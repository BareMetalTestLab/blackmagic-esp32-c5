#include "network-bmcloud-ws.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "m-string.h"
#include <ctype.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_websocket_client.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

/* --- Device websocket: presence and job push channel --- */

#define WS_HEARTBEAT_PERIOD_MS (10 * 1000)  // the server marks us offline after 30 s of silence
#define WS_BUFFER_SIZE         (1024)
#define WS_RX_MAX              (256)        // ack / error / job frames; bigger frames are dropped
#define WS_JOB_QUEUE_LEN       (4)          // the server replays pending jobs on reconnect
#define WS_REPORT_QUEUE_LEN    (8)          // at-least-once; drops are tolerable, silence is not

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
    mstring_t*                    host;
    mstring_t*                    token;
    volatile bool                 hello_pending;
    QueueHandle_t                 jobs;    // server pushes, the job loop consumes
    QueueHandle_t                 reports; // job reports, the sender task ships them
    uint8_t                       rx[WS_RX_MAX];
    size_t                        rx_len;
    bool                          rx_overflow;
};

/* ws(s) URL by the same rule as build_url: explicit scheme wins, bare IP is
 * plain ws for local testing, domains go through TLS */
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
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "Websocket error");
        break;
    default:
        break;
    }
}

/* Single writer for the websocket: capabilities right after the connection,
 * queued job reports, then a heartbeat every 10 s. A separate task from the
 * job loop, because flashing can hold the target for minutes and must not
 * stall the heartbeat. */
void bmcloud_ws_sender_task(void* pvParameters)
{
    bmcloud_ws_t* ws = (bmcloud_ws_t*) pvParameters;

    uint8_t    frame[192];

    while (1)
    {
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
            if (len > 0 && esp_websocket_client_send_bin(ws->handle, (char*) frame, len, pdMS_TO_TICKS(2000)) >= 0)
            {
                ESP_LOGI(TAG, "Capabilities sent (fw %s)", app->version);
                ws->hello_pending = false;
            }
        }

        // job reports go first: outcomes must not wait behind the heartbeat
        while (connected)
        {
            ws_report_t rep;
            if (xQueuePeek(ws->reports, &rep, 0) != pdTRUE)
                break;
            int len = bmchannel_encode_job_report(rep.job_id, rep.status, rep.error, frame, sizeof(frame));
            if (len <= 0 || esp_websocket_client_send_bin(ws->handle, (char*) frame, len, pdMS_TO_TICKS(2000)) < 0)
                break; // not delivered: keep it queued for the next round
            xQueueReceive(ws->reports, &rep, 0); // pop what was peeked
            connected = esp_websocket_client_is_connected(ws->handle);
        }

        vTaskDelay(pdMS_TO_TICKS(250));
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

    ws->jobs    = xQueueCreate(WS_JOB_QUEUE_LEN, sizeof(BmChannelJobType));
    ws->reports = xQueueCreate(WS_REPORT_QUEUE_LEN, sizeof(ws_report_t));
    if (ws->jobs == NULL || ws->reports == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate the websocket queues");
        vQueueDelete(ws->jobs);
        vQueueDelete(ws->reports);
        free(ws);
        return NULL;
    }

    xTaskCreate(bmcloud_ws_sender_task, "bmcloud_ws", 4096, ws, 5, NULL);

    return ws;
}

void bmcloud_ws_stop(bmcloud_ws_t* ws)
{
    if (ws == NULL || ws->handle == NULL)
        return;

    esp_websocket_client_stop(ws->handle);
    esp_websocket_client_destroy(ws->handle);
    ws->handle = NULL;

    mstring_free(ws->host);
    mstring_free(ws->token);
    ws->host  = NULL;
    ws->token = NULL;
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
