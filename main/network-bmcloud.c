#include "network-bmcloud.h"
#include "just_flasher.h"
#include "network-bmcloud-ws.h"
#include "freertos/idf_additions.h"
#include "m-string.h"
#include "nvs-config.h"
#include <psa/crypto.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

/* --- Job runner --- */

#define CLOUD_STARTUP_DELAY_MS     (5 * 1000)   // give the network time to come up after boot
#define CLOUD_POLL_PERIOD_MS       (500)        // job queue poll while idle
#define CLOUD_UNCONFIGURED_MS      (10 * 1000)  // re-read NVS while host/token are not set
#define CLOUD_FLASHER_LOCK_WAIT_MS (5 * 1000)  // bounded short: a longer wait turns "device busy" into a stall

#define FW_BEGIN_TIMEOUT_MS (15 * 1000)  // the transfer follows its Job frame immediately
#define FW_READ_TIMEOUT_MS  (10 * 1000)  // no chunk within this: the transfer is stalled
#define FW_END_TIMEOUT_MS   (3 * 1000)   // the End frame trails the last chunk closely
#define FW_CHUNK            (1024)       // flash write granularity
#define FW_ACK_STEP         (4 * 1024)   // cumulative ack granularity, paces the server window

static bool verify_flash_digest(const uint8_t* sha256, size_t written, char* buffer, size_t buffer_size)
{
    bool                 success = true;
    psa_hash_operation_t sha     = PSA_HASH_OPERATION_INIT;

    if (psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS)
    {
        return false;
    }

    for (size_t off = 0; off < written && success;)
    {
        size_t chunk = written - off < buffer_size ? written - off : buffer_size;
        if (just_flasher_read(FLASHER_DEFAULT_BASE_ADDR + off, buffer, chunk) == JustFlasherOk)
            psa_hash_update(&sha, (const uint8_t*) buffer, chunk);
        else
            success = false;
        off += chunk;
    }

    uint8_t digest[32];
    if (success && psa_hash_finish(&sha, digest, sizeof(digest), (size_t[]) {0}) != PSA_SUCCESS)
    {
        success = false;
    }
    psa_hash_abort(&sha);

    if (success && memcmp(digest, sha256, sizeof(digest)) != 0)
    {
        ESP_LOGW(TAG, "Flash SHA256 mismatch");
        return false;
    }

    return true;
}

/* What the job loop streams into the target flash */
typedef struct
{
    bmcloud_ws_t* ws;
    uint64_t      job_id;
    uint32_t      transfer_id;
    size_t        total;
    size_t        written;
    size_t        acked;
    int           last_progress;
} fw_sink_t;

/* Hash, write and account for one buffer of transferred firmware */
static bool fw_sink_write(fw_sink_t* sink, const char* buffer, size_t len, const char** error)
{
    JustFlasherError ferr = just_flasher_write(FLASHER_DEFAULT_BASE_ADDR + sink->written, buffer, len);
    if (ferr != JustFlasherOk)
    {
        *error = just_flasher_err_str(ferr);
        return false;
    }
    sink->written += len;

    if (sink->written - sink->acked >= FW_ACK_STEP || sink->written == sink->total)
    {
        bmcloud_ws_fw_transfer_ack(sink->ws, sink->transfer_id, sink->written);
        sink->acked = sink->written;
    }

    // Log progress every 10%; each step also refreshes the job on the server,
    // so a long flash is not reaped
    int progress = (int) (sink->written * 100 / sink->total);
    if (progress / 10 > sink->last_progress / 10)
    {
        ESP_LOGI(TAG, "Flash progress: %zu / %zu bytes (%d%%)", sink->written, sink->total, progress);
        sink->last_progress = progress;
        bmcloud_ws_job_report(sink->ws, sink->job_id, BmChannelJobFlashing, NULL);
    }
    return true;
}

static bool flash_from_transfer(bmcloud_ws_t* ws, const BmChannelJobType* job, const char** error)
{
    *error = "firmware transfer failed";

    BmChannelTransferInfoType info;
    if (!bmcloud_ws_fw_transfer_wait_begin(ws, FW_BEGIN_TIMEOUT_MS, &info))
    {
        *error =
            bmcloud_ws_fw_transfer_error(ws) != NULL ? bmcloud_ws_fw_transfer_error(ws) : "no transfer from the server";
        return false;
    }
    if (info.total_size != job->size)
    {
        ESP_LOGE(TAG,
                 "Transfer %u size %llu, job says %llu",
                 (unsigned) info.transfer_id,
                 (unsigned long long) info.total_size,
                 (unsigned long long) job->size);
        *error = "transfer size mismatch";
        return false;
    }
    if (info.total_size == 0 || info.total_size > 16 * 1024 * 1024)
    {
        *error = "bad firmware size";
        return false;
    }

    JustFlasherError ferr = just_flasher_begin(true, CLOUD_FLASHER_LOCK_WAIT_MS);
    if (ferr != JustFlasherOk)
    {
        *error = just_flasher_err_str(ferr);
        return false;
    }

    char buffer[FW_CHUNK];

    fw_sink_t sink = {
        .ws            = ws,
        .job_id        = job->job_id,
        .transfer_id   = info.transfer_id,
        .total         = (size_t) info.total_size,
        .written       = 0,
        .acked         = 0,
        .last_progress = -1,
    };

    bool ok = false;

    // erase before the first write: an erase after a write discards the
    // driver's unwritten tail, punching 1 KB holes in the image
    ferr = just_flasher_erase(FLASHER_DEFAULT_BASE_ADDR, sink.total);
    if (ferr != JustFlasherOk)
    {
        *error = just_flasher_err_str(ferr);
        just_flasher_abort();
        goto out;
    }
    bmcloud_ws_job_report(ws, job->job_id, BmChannelJobFlashing, NULL);  // keep the watchdog fresh

    // Everything is erased: drain the rest of the transfer
    while (sink.written < sink.total)
    {
        if (bmcloud_ws_fw_transfer_error(ws) != NULL)
            break;
        size_t n = bmcloud_ws_fw_transfer_read(ws, buffer, sizeof(buffer), FW_READ_TIMEOUT_MS);
        if (n == 0)
            break;
        if (!fw_sink_write(&sink, buffer, n, error))
        {
            just_flasher_abort();
            goto out;
        }
    }

    if (sink.written != sink.total || bmcloud_ws_fw_transfer_error(ws) != NULL)
    {
        *error = bmcloud_ws_fw_transfer_error(ws) != NULL ? bmcloud_ws_fw_transfer_error(ws) : "transfer stalled";
        just_flasher_abort();
        goto out;
    }

    // The End frame trails the last chunk closely; wait it out
    for (int i = 0; i < FW_END_TIMEOUT_MS / 100 && !bmcloud_ws_fw_transfer_is_done(ws); i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (!bmcloud_ws_fw_transfer_is_done(ws))
    {
        *error = "transfer end missing";
        just_flasher_abort();
        goto out;
    }

    ferr = just_flasher_complete();
    if (ferr != JustFlasherOk)
    {
        *error = just_flasher_err_str(ferr);
        just_flasher_abort();
        goto out;
    }

    if (!verify_flash_digest(job->sha256, sink.written, buffer, sizeof(buffer)))
    {
        *error = "firmware digest mismatch";
        just_flasher_abort();
        goto out;
    }

    // Reset the target and release it
    ferr = just_flasher_end();
    if (ferr != JustFlasherOk)
    {
        *error = just_flasher_err_str(ferr);
        goto out;
    }
    ESP_LOGI(TAG, "Flashed %zu firmware bytes", sink.written);
    ok = true;

out:
    return ok;
}

/* Run one pushed job: receive the firmware transfer, flash it, report the
 * outcome. The websocket reports are queued and shipped by the sender task,
 * so a dropped connection does not lose the result. A failed job resyncs the
 * receiver in place and keeps the connection: the failure report then lands
 * on the live socket, ahead of any reconnect replay. Returns the outcome. */
static bool run_job(bmcloud_ws_t* ws, const BmChannelJobType* job)
{
    ESP_LOGI(TAG,
             "Running job %llu: firmware %llu, %llu bytes",
             (unsigned long long) job->job_id,
             (unsigned long long) job->firmware_id,
             (unsigned long long) job->size);

    bmcloud_ws_job_report(ws, job->job_id, BmChannelJobFlashing, NULL);  // claims the job

    const char* error = "firmware transfer failed";
    bool        ok    = flash_from_transfer(ws, job, &error);

    if (ok)
        ESP_LOGI(TAG, "Job %llu done", (unsigned long long) job->job_id);
    else
    {
        ESP_LOGE(TAG, "Job %llu failed: %s", (unsigned long long) job->job_id, error);
        // resync in place: the failure report ships on the live socket
        bmcloud_ws_fw_transfer_reset(ws);
    }

    bmcloud_ws_job_report(ws, job->job_id, ok ? BmChannelJobDone : BmChannelJobFailed, ok ? NULL : error);
    return ok;
}

static void network_bmcloud_task(void* pvParameters)
{
    (void) pvParameters;

    vTaskDelay(pdMS_TO_TICKS(CLOUD_STARTUP_DELAY_MS));

    bmcloud_ws_t* ws = bmcloud_ws_start();

    // at-least-once delivery: the server replays pending jobs on reconnect,
    // so skip whatever already ran and re-report its outcome
    uint64_t last_job_id = 0;
    bool     last_job_ok = false;

    mstring_t* host  = mstring_alloc();
    mstring_t* token = mstring_alloc();

    while (1)
    {
        bool configured = nvs_config_get_cloud_host(host) == ESP_OK && nvs_config_get_cloud_token(token) == ESP_OK;

        if (!configured)
        {
            ESP_LOGW(TAG, "Cloud host/token are not set, waiting for configuration");
            bmcloud_ws_stop(ws);
            vTaskDelay(pdMS_TO_TICKS(CLOUD_UNCONFIGURED_MS));
            continue;
        }

        // Presence, job pushes and firmware transfers; the client reconnects on its own
        bmcloud_ws_ensure(ws, host, token);

        BmChannelJobType job;
        if (bmcloud_ws_job_pop(ws, &job, pdMS_TO_TICKS(CLOUD_POLL_PERIOD_MS)))
        {
            if (job.job_id == last_job_id)
            {
                ESP_LOGI(TAG, "Job %llu already ran, skipped", (unsigned long long) job.job_id);
                bmcloud_ws_fw_transfer_reset(ws);
                bmcloud_ws_job_report(ws,
                                      job.job_id,
                                      last_job_ok ? BmChannelJobDone : BmChannelJobFailed,
                                      last_job_ok ? NULL : "job already ran");
            }
            else
            {
                last_job_id = job.job_id;
                last_job_ok = run_job(ws, &job);
            }
        }
    }
    mstring_free(host);
    mstring_free(token);
}

void network_bmcloud_init(void)
{
    // Transfer digests ride PSA crypto; TLS may have initialized it already,
    // a second call is a harmless no-op
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS)
        ESP_LOGE(TAG, "psa_crypto_init failed: %ld", (long) status);

    // 8 KB stack: the transfer drain path and the digests need headroom
    xTaskCreate(network_bmcloud_task, "network_bmcloud_task", 8192, NULL, 5, NULL);
}
