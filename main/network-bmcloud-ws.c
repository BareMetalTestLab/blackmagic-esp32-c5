#include "network-bmcloud-ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "m-string.h"
#include <ctype.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_websocket_client.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

/* --- Device websocket: presence, job push and firmware transfer channel --- */

#define WS_BUFFER_SIZE (1024)  // transport read granularity; frames reassemble regardless
#define WS_RX_MAX      (16 * 1024 + 512)         // a whole TransferChunk frame plus envelope overhead
#define WS_STREAM_SIZE (16 * 1024 + 512)         // one chunk in flight; TCP backpressure holds the rest
#define WS_JOB_QUEUE_LEN       (4)               // the server replays pending jobs on reconnect
#define WS_REPORT_QUEUE_LEN    (8)               // at-least-once; drops are tolerable, silence is not
#define WS_SEND_BLOCK_MS       (30 * 1000)       // stream write bound: the up-front image erase takes ~15 s
#define WS_TX_TIMEOUT_MS       (500)            // sends share only the tx lock with pings (SEPARATE_TX_LOCK); the bound just notices a dead peer
#define WS_ACK_TIMEOUT_MS      (10 * 1000)       // a stalled receiver fails the transfer

/* Event bits: wake the sender, signal a fresh TransferBegin to the job loop */
#define WS_EVT_WAKE     (1 << 0)
#define WS_EVT_FW_BEGIN (1 << 1)

/* What this stand can serve; the server shows the list to consumers */
static const char* const ws_peripherals[] = {"swd", "rtt0", "uart0"};

typedef struct
{
    uint64_t               job_id;
    BmChannelJobStatusType status;
    char                   error[96];
} ws_report_t;

struct bmcloud_ws_t
{
    esp_websocket_client_handle_t handle;
    SemaphoreHandle_t             send_lock;  // serializes sends against stop/destroy
    StaticSemaphore_t             send_lock_mem;
    mstring_t*                    host;
    mstring_t*                    token;
    volatile bool                 hello_pending;
    QueueHandle_t                 jobs;    // server pushes, the job loop consumes
    QueueHandle_t                 reports; // job reports, the sender task ships them
    EventGroupHandle_t            events;
    uint8_t                       rx[WS_RX_MAX];
    size_t                        rx_len;
    bool                          rx_overflow;

    /* Firmware transfer receiver: one byte stream, transfers strictly
     * sequential on the wire (Begin → chunks → End → next Begin). */
    StreamBufferHandle_t          fw_stream;    // chunk payloads, the job loop drains them into flash
    StaticStreamBuffer_t          fw_stream_mem;
    uint8_t                       fw_stream_buf[WS_STREAM_SIZE];
    volatile uint32_t             fw_seq;       // increments per Begin
    volatile uint32_t             fw_consumed;  // last seq the job loop waited for
    volatile uint32_t             fw_id;        // active transfer
    volatile uint64_t             fw_total;
    volatile uint64_t             fw_rx_bytes;  // bytes pushed into the stream this transfer
    volatile bool                 fw_end;
    volatile const char*          fw_error;

    /* Pending cumulative ack, shipped by the sender task */
    portMUX_TYPE                  ack_lock;
    bool                          ack_pending;
    uint32_t                      ack_transfer_id;
    uint64_t                      ack_received;
};

/* Send a frame unless the socket is being torn down. The job loop may stop
 * and destroy the client at any moment (a failed job, a config change):
 * without this lock the sender would race a send against destroy and touch
 * a freed client. */
static int ws_send(bmcloud_ws_t* ws, const uint8_t* frame, int len)
{
    xSemaphoreTake(ws->send_lock, portMAX_DELAY);
    int res = -1;
    if (ws->handle != NULL)
        res = esp_websocket_client_send_bin(ws->handle, (const char*) frame, len, pdMS_TO_TICKS(WS_TX_TIMEOUT_MS));
    xSemaphoreGive(ws->send_lock);
    return res;
}

/* ws(s) URL: explicit scheme wins, bare IP is plain ws for local testing,
 * domains go through TLS */
static void build_ws_url(char* dst, size_t dst_size, const char* host)
{
    if (strncmp(host, "http://", 7) == 0)
    {
        snprintf(dst, dst_size, "ws://%s/device/ws", host + 7);
        return;
    }
    if (strncmp(host, "https://", 8) == 0)
    {
        snprintf(dst, dst_size, "wss://%s/device/ws", host + 8);
        return;
    }

    bool is_ip = true;
    for (const char* p = host; *p != '\0'; p++)
    {
        if (!isdigit((unsigned char) *p) && *p != '.' && *p != ':')
        {
            is_ip = false;
            break;
        }
    }

    snprintf(dst, dst_size, "%s://%s/device/ws", is_ip ? "ws" : "wss", host);
}

/* Push one chunk payload into the stream, waiting out a slow consumer
 * (the up-front image erase or the post-write readback). Short slices with
 * an error check between them: a failed job cancels the receiver within one
 * slice instead of blocking the websocket client task — and its locks — for
 * the whole budget. Past the overall bound the transfer fails. */
static void fw_feed(bmcloud_ws_t* ws, const uint8_t* data, size_t len)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(WS_SEND_BLOCK_MS);

    size_t done = 0;
    while (done < len)
    {
        if (ws->fw_error != NULL)
            break; // the job loop gave up: drop the rest of the transfer

        TickType_t left = deadline - xTaskGetTickCount();
        if ((int32_t) left <= 0)
            break;

        size_t want = len - done;
        if (want > 1024)
            want = 1024;
        size_t n = xStreamBufferSend(ws->fw_stream, data + done, want, left < pdMS_TO_TICKS(250) ? left : pdMS_TO_TICKS(250));
        if (n == 0 && left <= pdMS_TO_TICKS(250))
            break; // the budget is exhausted
        done += n;
    }

    ws->fw_rx_bytes += done;
    if (done < len && ws->fw_error == NULL)
    {
        ESP_LOGE(TAG, "Transfer %u receiver stalled, failing the transfer", ws->fw_id);
        ws->fw_error = "receive buffer stalled";
    }
}

/* One server frame may arrive split across several DATA events: reassemble
 * by payload_offset, decode once complete. Frames bigger than the buffer are
 * swallowed whole and dropped. */
static void ws_handle_frame(bmcloud_ws_t* ws, esp_websocket_event_data_t* data)
{
    if (data->payload_offset == 0)
    {
        ws->rx_len      = 0;
        ws->rx_overflow = false;
    }
    if (ws->rx_overflow)
        return;

    if (ws->rx_len + data->data_len > sizeof(ws->rx))
    {
        ESP_LOGW(TAG, "Server frame of %d bytes is too big, dropped", data->payload_len);
        ws->rx_overflow = true;
        return;
    }

    memcpy(ws->rx + ws->rx_len, data->data_ptr, data->data_len);
    ws->rx_len += data->data_len;
    if (ws->rx_len < (size_t) data->payload_len)
        return; // the frame continues in the next event

    BmChannelServerMsgType msg;
    if (!bmchannel_decode(ws->rx, ws->rx_len, &msg))
    {
        ESP_LOGW(TAG, "Malformed server frame dropped: len=%u", (unsigned) ws->rx_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, ws->rx, ws->rx_len, ESP_LOG_WARN);
        return;
    }

    switch (msg.kind)
    {
    case BmChannelServerJob:
        ESP_LOGI(TAG,
                 "Job %llu pushed: firmware %llu, %llu bytes",
                 (unsigned long long) msg.as.job.job_id,
                 (unsigned long long) msg.as.job.firmware_id,
                 (unsigned long long) msg.as.job.size);
        if (xQueueSend(ws->jobs, &msg.as.job, 0) != pdTRUE)
            ESP_LOGW(TAG, "Job queue full, the push is replayed on reconnect");
        break;
    case BmChannelServerTransferBegin:
        ESP_LOGI(TAG,
                 "Transfer %u begins: %llu bytes",
                 (unsigned) msg.as.begin.transfer_id,
                 (unsigned long long) msg.as.begin.total_size);
        ws->fw_id       = msg.as.begin.transfer_id;
        ws->fw_total    = msg.as.begin.total_size;
        ws->fw_rx_bytes = 0;
        ws->fw_end      = false;
        ws->fw_seq++;
        xEventGroupSetBits(ws->events, WS_EVT_FW_BEGIN);
        break;
    case BmChannelServerTransferChunk:
        // after a failure the rest of the transfer is drained and dropped;
        // the job loop forces a reconnect to resynchronize
        if (ws->fw_error != NULL)
            break;
        if (msg.as.chunk.transfer_id != ws->fw_id)
        {
            // id 0 follows an in-place reset: dropping the old transfer's
            // remainder is expected, not worth a warning per chunk
            if (ws->fw_id != 0)
                ESP_LOGW(TAG, "Chunk of unknown transfer %u dropped", (unsigned) msg.as.chunk.transfer_id);
            break;
        }
        if (msg.as.chunk.offset != ws->fw_rx_bytes)
        {
            ESP_LOGE(TAG, "Transfer %u chunk offset %llu, expected %llu",
                     (unsigned) msg.as.chunk.transfer_id,
                     (unsigned long long) msg.as.chunk.offset,
                     (unsigned long long) ws->fw_rx_bytes);
            ws->fw_error = "chunk offset mismatch";
            break;
        }
        fw_feed(ws, msg.as.chunk.data, msg.as.chunk.data_len);
        break;
    case BmChannelServerTransferEnd:
        ESP_LOGI(TAG, "Transfer %u complete on the wire", (unsigned) msg.as.end.transfer_id);
        if (msg.as.end.transfer_id == ws->fw_id)
            ws->fw_end = true;
        break;
    case BmChannelServerError:
        ESP_LOGW(TAG, "Server error: %s", msg.as.error);
        break;
    case BmChannelServerAck:
        break;
    default:
        ESP_LOGD(TAG, "Unhandled server frame, skipped");
        break;
    }
}

static void ws_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data)
{
    bmcloud_ws_t* ws = (bmcloud_ws_t*) arg;
    (void) base;

    esp_websocket_event_data_t* data = event_data;
    switch (event_id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Websocket connected");
        // the sender task picks it up: single writer for the socket
        ws->hello_pending = true;
        xEventGroupSetBits(ws->events, WS_EVT_WAKE);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data != NULL)
        {
            if (data->op_code == 0x02 || data->op_code == 0x00)
            {
                if (data->data_len > 0)
                    ws_handle_frame(ws, data);
            }
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Websocket disconnected, the client reconnects on its own");
        // a dropped transfer cannot resume: the job loop restarts the socket
        if (ws->fw_error == NULL && !ws->fw_end)
            ws->fw_error = "connection reset";
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "Websocket error");
        break;
    default:
        break;
    }
}

/* Single writer for the websocket: capabilities right after the connection,
 * then queued job reports and transfer acks, woken on demand. A separate
 * task from the job loop, because flashing can hold the target for minutes
 * and must not stall the channel. */
void bmcloud_ws_sender_task(void* pvParameters)
{
    bmcloud_ws_t* ws = (bmcloud_ws_t*) pvParameters;

    uint8_t frame[192];

    while (1)
    {
        xEventGroupWaitBits(ws->events, WS_EVT_WAKE, pdTRUE, pdFALSE, pdMS_TO_TICKS(250));

        if (ws->handle == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        bool connected = esp_websocket_client_is_connected(ws->handle);

        if (connected && ws->hello_pending)
        {
            const esp_app_desc_t* app = esp_app_get_description();
            int len = bmchannel_encode_capabilities(
                app->version,
                ws_peripherals,
                sizeof(ws_peripherals) / sizeof(ws_peripherals[0]),
                frame,
                sizeof(frame));
            if (len > 0 && ws_send(ws, frame, len) >= 0)
            {
                ESP_LOGI(TAG, "Capabilities sent (fw %s)", app->version);
                ws->hello_pending = false;
            }
        }

        while (connected)
        {
            // a transfer ack first: the server's send window waits on it
            taskENTER_CRITICAL(&ws->ack_lock);
            bool     pending = ws->ack_pending;
            uint32_t id      = ws->ack_transfer_id;
            uint64_t bytes   = ws->ack_received;
            taskEXIT_CRITICAL(&ws->ack_lock);

            int len = -1;
            if (pending)
                len = bmchannel_encode_transfer_ack(id, bytes, frame, sizeof(frame));
            if (!pending || len <= 0 || ws_send(ws, frame, len) < 0)
                break; // not delivered: keep it pending for the next round

            taskENTER_CRITICAL(&ws->ack_lock);
            if (ws->ack_transfer_id == id && ws->ack_received == bytes)
                ws->ack_pending = false; // a newer ack may already wait: keep it
            taskEXIT_CRITICAL(&ws->ack_lock);
            connected = ws->handle != NULL;
        }

        // job reports follow: outcomes must not wait behind the window acks
        while (connected)
        {
            ws_report_t rep;
            if (xQueuePeek(ws->reports, &rep, 0) != pdTRUE)
                break;
            int len = bmchannel_encode_job_report(rep.job_id, rep.status, rep.error, frame, sizeof(frame));
            if (len <= 0 || ws_send(ws, frame, len) < 0)
                break; // not delivered: keep it queued for the next round
            xQueueReceive(ws->reports, &rep, 0); // pop what was peeked
            connected = ws->handle != NULL;
        }
    }
}

bmcloud_ws_t* bmcloud_ws_start(void)
{
    bmcloud_ws_t* ws = malloc(sizeof(bmcloud_ws_t));
    if (ws == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate bmcloud_ws_t");
        return NULL;
    }
    memset(ws, 0, sizeof(bmcloud_ws_t));
    ws->send_lock = xSemaphoreCreateMutexStatic(&ws->send_lock_mem);

    ws->jobs    = xQueueCreate(WS_JOB_QUEUE_LEN, sizeof(BmChannelJobType));
    ws->reports = xQueueCreate(WS_REPORT_QUEUE_LEN, sizeof(ws_report_t));
    ws->events  = xEventGroupCreate();
    ws->fw_stream = xStreamBufferCreateStatic(
        sizeof(ws->fw_stream_buf), 1, ws->fw_stream_buf, &ws->fw_stream_mem);
    if (ws->jobs == NULL || ws->reports == NULL || ws->events == NULL || ws->fw_stream == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate the websocket queues");
        if (ws->jobs != NULL)
            vQueueDelete(ws->jobs);
        if (ws->reports != NULL)
            vQueueDelete(ws->reports);
        if (ws->events != NULL)
            vEventGroupDelete(ws->events);
        free(ws);
        return NULL;
    }

    xTaskCreate(bmcloud_ws_sender_task, "bmcloud_ws", 4096, ws, 5, NULL);

    return ws;
}

/* Drop the websocket and reset the transfer receiver; the next ensure()
 * reconnects and the server replays what is still pending */
void bmcloud_ws_stop(bmcloud_ws_t* ws)
{
    if (ws == NULL)
        return;

    // Under the send lock: the sender task may be inside a send on this very
    // client, destroy must not race it (use-after-free panic otherwise)
    if (ws->handle != NULL)
    {
        xSemaphoreTake(ws->send_lock, portMAX_DELAY);
        esp_websocket_client_handle_t handle = ws->handle;
        ws->handle = NULL;
        xSemaphoreGive(ws->send_lock);

        esp_websocket_client_stop(handle);
        esp_websocket_client_destroy(handle);
    }

    // mstring_free(NULL) asserts: the socket may be stopped before ever
    // having been started (unconfigured, failed init)
    if (ws->host != NULL)
    {
        mstring_free(ws->host);
        ws->host = NULL;
    }
    if (ws->token != NULL)
    {
        mstring_free(ws->token);
        ws->token = NULL;
    }

    // receiver state gets the same in-place resync; the handler task is gone
    // with the client, so the stream reset cannot contend with a feed
    bmcloud_ws_fw_reset(ws);
}

bool bmcloud_ws_pop_job(bmcloud_ws_t* ws, BmChannelJobType* job)
{
    if (ws == NULL || ws->jobs == NULL)
        return false;
    return xQueueReceive(ws->jobs, job, 0) == pdTRUE;
}

void bmcloud_ws_report_job(bmcloud_ws_t* ws, uint64_t job_id, BmChannelJobStatusType status, const char* error)
{
    if (ws == NULL || ws->reports == NULL)
        return;

    ws_report_t rep = {
        .job_id = job_id,
        .status = status,
    };
    if (error != NULL)
        snprintf(rep.error, sizeof(rep.error), "%s", error);

    if (xQueueSend(ws->reports, &rep, 0) != pdTRUE)
        ESP_LOGW(TAG, "Report queue full, a report was dropped");
    else
        xEventGroupSetBits(ws->events, WS_EVT_WAKE);
}

bool bmcloud_ws_fw_wait_begin(bmcloud_ws_t* ws, uint32_t timeout_ms, BmChannelTransferInfoType* info)
{
    if (ws == NULL || ws->fw_stream == NULL)
        return false;

    uint32_t seen = ws->fw_consumed;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (ws->fw_seq == seen)
    {
        TickType_t left = deadline - xTaskGetTickCount();
        if ((int32_t) left <= 0)
            return false;
        xEventGroupWaitBits(ws->events, WS_EVT_FW_BEGIN, pdTRUE, pdFALSE, left);
        if (ws->fw_error != NULL)
            return false;
    }
    ws->fw_consumed = ws->fw_seq;

    if (ws->fw_error != NULL)
        return false;
    info->transfer_id = ws->fw_id;
    info->total_size  = ws->fw_total;
    return true;
}

size_t bmcloud_ws_fw_read(bmcloud_ws_t* ws, void* buf, size_t len, uint32_t timeout_ms)
{
    if (ws == NULL || ws->fw_stream == NULL || len == 0)
        return 0;
    return xStreamBufferReceive(ws->fw_stream, buf, len, pdMS_TO_TICKS(timeout_ms));
}

bool bmcloud_ws_fw_is_done(bmcloud_ws_t* ws)
{
    if (ws == NULL || ws->fw_stream == NULL)
        return false;
    return ws->fw_end && xStreamBufferBytesAvailable(ws->fw_stream) == 0;
}

const char* bmcloud_ws_fw_error(bmcloud_ws_t* ws)
{
    if (ws == NULL)
        return "no websocket";
    return (const char*) ws->fw_error;
}

/* Cancel the receiver from the job loop side (e.g. the flasher failed while
 * the transfer keeps arriving): the handler drops the rest within one slice
 * instead of blocking on a full stream until the budget expires */
void bmcloud_ws_fw_fail(bmcloud_ws_t* ws, const char* reason)
{
    if (ws == NULL || ws->fw_error != NULL)
        return;
    if (reason == NULL)
        reason = "receiver cancelled";
    ws->fw_error = reason;
}

void bmcloud_ws_fw_reset(bmcloud_ws_t* ws)
{
    if (ws == NULL || ws->fw_stream == NULL)
        return;

    // keep the failure set while resetting: it is what makes the handler
    // drop the rest of the old transfer instead of feeding it back in
    if (ws->fw_error == NULL)
        ws->fw_error = "receiver reset";

    // Release the server's send window before wiping the id: transfers are
    // strictly sequential there and one that nobody acks parks the whole
    // queue — the next job would wait out its begin timeout behind a dead
    // transfer. Acknowledging the full size makes the server flush the rest
    // (dropped below), send its End and move on; job status is untouched,
    // outcomes travel only in job reports
    if (ws->fw_id != 0)
        bmcloud_ws_fw_ack(ws, ws->fw_id, ws->fw_total);

    ws->fw_id = 0; // matches no live transfer: the old one's late chunks are dropped

    // the handler may sit in a bounded stream send: retry until it lets go
    for (int i = 0; i < WS_ACK_TIMEOUT_MS / 100 && xStreamBufferReset(ws->fw_stream) != pdPASS; i++)
        vTaskDelay(pdMS_TO_TICKS(100));

    ws->fw_error = NULL;
    ws->fw_end   = false;
}

void bmcloud_ws_fw_ack(bmcloud_ws_t* ws, uint32_t transfer_id, uint64_t received_bytes)
{
    if (ws == NULL)
        return;

    taskENTER_CRITICAL(&ws->ack_lock);
    ws->ack_pending     = true; // cumulative: the latest value subsumes the older ones
    ws->ack_transfer_id = transfer_id;
    ws->ack_received    = received_bytes;
    taskEXIT_CRITICAL(&ws->ack_lock);

    xEventGroupSetBits(ws->events, WS_EVT_WAKE);
}

/* Start the websocket once host/token are known; restart when they change in the settings */
void bmcloud_ws_ensure(bmcloud_ws_t* ws, const mstring_t* host, const mstring_t* token)
{
    if (ws->handle != NULL && ws->host != NULL && ws->token != NULL &&
        mstring_cmp_cstr(ws->host, mstring_get_cstr(host)) == 0 &&
        mstring_cmp_cstr(ws->token, mstring_get_cstr(token)) == 0)
        return;  // already running with this config

    bmcloud_ws_stop(ws);

    char url[256];
    build_ws_url(url, sizeof(url), mstring_get_cstr(host));

    char headers[320];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", mstring_get_cstr(token));

    esp_websocket_client_config_t config = {
        .uri                  = url,
        .headers              = headers,
        .buffer_size          = WS_BUFFER_SIZE,
        .reconnect_timeout_ms = 10 * 1000,  // the client reconnects on its own
    };
    if (strncmp(url, "wss://", 6) == 0)
        config.crt_bundle_attach = esp_crt_bundle_attach;

    ws->handle = esp_websocket_client_init(&config);
    if (ws->handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to init the websocket client");
        return;
    }

    esp_websocket_register_events(ws->handle, WEBSOCKET_EVENT_ANY, ws_event_handler, ws);
    esp_websocket_client_start(ws->handle);

    // remember what we started with, so a settings change restarts the socket
    ws->host  = mstring_alloc();
    ws->token = mstring_alloc();
    mstring_set(ws->host, mstring_get_cstr(host));
    mstring_set(ws->token, mstring_get_cstr(token));
}
