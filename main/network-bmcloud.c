#include "network-bmcloud.h"
#include "just_flasher.h"
#include "network-bmcloud-ws.h"
#include "freertos/idf_additions.h"
#include "m-string.h"
#include "nvs-config.h"
#include <ctype.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <mbedtls/private/sha256.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

/* Jobs arrive as pushes on the device websocket (bmcloud-channel frames);
 * this file runs them: downloads the firmware over REST, flashes it and
 * reports the outcome back over the websocket. */

/* Build request URL from the stored host.
 * Host may be a bare domain, a bare IP or a full URL with scheme:
 *   paulfirs.com      -> https://paulfirs.com/...
 *   192.168.1.50      -> http://192.168.1.50/...  (local testing)
 *   http://192.168.1.50:8080 -> scheme wins
 */
static void build_url(char* dst, size_t dst_size, const char* host, const char* path)
{
    if (strncmp(host, "http://", 7) == 0 || strncmp(host, "https://", 8) == 0)
    {
        snprintf(dst, dst_size, "%s%s", host, path);
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

    snprintf(dst, dst_size, "%s://%s%s", is_ip ? "http" : "https", host, path);
}

/* --- Job runner --- */

#define CLOUD_STARTUP_DELAY_MS     (5 * 1000)        // give the network time to come up after boot
#define CLOUD_POLL_PERIOD_MS       (500)             // job queue poll while idle
#define CLOUD_UNCONFIGURED_MS      (10 * 1000)       // re-read NVS while host/token are not set
#define FIRMWARE_CHUNK_SIZE        (1024)            // download read chunk
#define FIRMWARE_MAX_ATTEMPTS      (3)               // download retries before reporting a failed job
#define CLOUD_FLASHER_LOCK_WAIT_MS (10 * 60 * 1000)  // wait out a concurrent web flash before failing the job

/* The websocket is shared with network_bmcloud_set_status callers */
static bmcloud_ws_t* s_ws = NULL;

void network_bmcloud_set_status(CloudDeviceStateType state)
{
    // the REST state endpoint is gone: presence state rides the heartbeat now
    if (s_ws == NULL)
        return;

    switch (state)
    {
    case CloudDeviceStateBusy:
        bmcloud_ws_set_state(s_ws, BmChannelStateBusy);
        break;
    case CloudDeviceStateError:
        bmcloud_ws_set_state(s_ws, BmChannelStateError);
        break;
    case CloudDeviceStateOnline:
        bmcloud_ws_set_state(s_ws, BmChannelStateOnline);
        break;
    case CloudDeviceStateOffline:
    default:
        // offline is the server's conclusion from silence, not something we report
        break;
    }
}

/* Verify what actually landed in the target flash: the download is streamed
 * straight into the target, so read the image back and hash it against the
 * digest pushed with the job. Catches both a corrupted download and a bad
 * write. On failure *error points to the reason. */
static bool verify_flash_digest(const BmChannelJobType* job, size_t written, char* buffer, size_t buffer_size, const char** error)
{
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    bool ok = true;
    for (size_t off = 0; off < written && ok; )
    {
        size_t chunk = written - off < buffer_size ? written - off : buffer_size;
        JustFlasherError ferr = just_flasher_read(FLASHER_DEFAULT_BASE_ADDR + off, buffer, chunk);
        if (ferr == JustFlasherOk)
            mbedtls_sha256_update(&sha, (const unsigned char*) buffer, chunk);
        else
        {
            *error = just_flasher_err_str(ferr);
            ok     = false;
        }
        off += chunk;
    }

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!ok)
        return false;

    if (memcmp(digest, job->sha256, sizeof(digest)) != 0)
    {
        ESP_LOGW(TAG, "Flash SHA256 mismatch");
        *error = "sha256 mismatch";
        return false;
    }

    return true;
}

/* GET /device/firmware/{id}: download the firmware and stream it into the
 * target flash. On failure *error points to a short reason for the job report. */
static bool download_and_flash(
    bmcloud_ws_t*         ws,
    const BmChannelJobType* job,
    const char*           host,
    const char*           token,
    const char**          error)
{
    *error = "firmware download failed";

    char path[64];
    snprintf(path, sizeof(path), "/device/firmware/%llu", (unsigned long long) job->firmware_id);

    char url[256];
    build_url(url, sizeof(url), host, path);

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    bool ok = false;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET firmware failed: %s", esp_err_to_name(err));
        goto out;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status         = esp_http_client_get_status_code(client);
    if (status != HttpStatus_Ok)
    {
        ESP_LOGE(TAG, "HTTP GET firmware: unexpected status = %d", status);
        goto out;
    }

    {
        JustFlasherError ferr = just_flasher_begin(true, CLOUD_FLASHER_LOCK_WAIT_MS);
        if (ferr != JustFlasherOk)
        {
            *error = just_flasher_err_str(ferr);
            goto out;
        }

        // Erase exactly what we are about to download, or the whole main bank
        // when the server does not report the size (chunked response gives -1)
        ferr = just_flasher_erase(FLASHER_DEFAULT_BASE_ADDR, content_length > 0 ? (size_t) content_length : 0);
        if (ferr != JustFlasherOk)
        {
            *error = just_flasher_err_str(ferr);
            just_flasher_abort();
            goto out;
        }

        char   buffer[FIRMWARE_CHUNK_SIZE];
        size_t written                = 0;
        int    last_progress_reported = -1;
        mbedtls_sha256_context download_sha;
        mbedtls_sha256_init(&download_sha);
        mbedtls_sha256_starts(&download_sha, 0);

        while (1)
        {
            int data_read = esp_http_client_read(client, buffer, sizeof(buffer));
            if (data_read < 0)
            {
                ESP_LOGE(TAG, "Firmware download interrupted after %zu bytes", written);
                *error = "firmware download interrupted";
                just_flasher_abort();
                goto out;
            }
            if (data_read == 0)
                break;

            mbedtls_sha256_update(&download_sha, (const unsigned char*) buffer, (size_t) data_read);

            ferr = just_flasher_write(FLASHER_DEFAULT_BASE_ADDR + written, buffer, data_read);
            if (ferr != JustFlasherOk)
            {
                *error = just_flasher_err_str(ferr);
                just_flasher_abort();
                goto out;
            }
            written += data_read;

            // Log progress every 10% when the size is known; each step also
            // refreshes the job on the server, so a long flash is not reaped
            if (content_length > 0)
            {
                int progress = (int) (written * 100 / (size_t) content_length);
                if (progress / 10 > last_progress_reported / 10)
                {
                    ESP_LOGI(TAG, "Flash progress: %zu / %d bytes (%d%%)", written, content_length, progress);
                    last_progress_reported = progress;
                    bmcloud_ws_report_job(ws, job->job_id, BmChannelJobFlashing, NULL);
                }
            }
        }

        uint8_t download_digest[32];
        mbedtls_sha256_finish(&download_sha, download_digest);
        mbedtls_sha256_free(&download_sha);

        if (job->has_sha256 && memcmp(download_digest, job->sha256, sizeof(download_digest)) != 0)
            ESP_LOGW(TAG, "Downloaded firmware SHA256 mismatch");

        if (written != job->size)
        {
            ESP_LOGE(TAG, "Firmware size mismatch: got %zu of %llu bytes", written, (unsigned long long) job->size);
            *error = "firmware size mismatch";
            just_flasher_abort();
            goto out;
        }

        JustFlasherError complete_error = just_flasher_complete();
        if (complete_error != JustFlasherOk)
        {
            *error = just_flasher_err_str(complete_error);
            just_flasher_abort();
            goto out;
        }

        if (job->has_sha256 && !verify_flash_digest(job, written, buffer, sizeof(buffer), error))
        {
            just_flasher_abort();
            goto out;
        }

        // Reset the target and release it
        JustFlasherError end_error = just_flasher_end();
        if (end_error != JustFlasherOk)
        {
            *error = just_flasher_err_str(end_error);
            goto out;
        }
        ESP_LOGI(TAG, "Flashed %zu firmware bytes", written);
        ok = true;
    }

out:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* Run one pushed job: download the firmware, flash it, report the outcome.
 * The websocket reports are queued and shipped by the sender task, so a
 * dropped connection does not lose the result. */
static void run_job(bmcloud_ws_t* ws, const mstring_t* host, const mstring_t* token, const BmChannelJobType* job)
{
    ESP_LOGI(TAG,
             "Running job %llu: firmware %llu, %llu bytes",
             (unsigned long long) job->job_id,
             (unsigned long long) job->firmware_id,
             (unsigned long long) job->size);

    bmcloud_ws_set_state(ws, BmChannelStateBusy);
    bmcloud_ws_report_job(ws, job->job_id, BmChannelJobFlashing, NULL); // claims the job

    const char* error = "firmware download failed";
    bool        ok    = false;

    for (int attempt = 1; attempt <= FIRMWARE_MAX_ATTEMPTS && !ok; attempt++)
    {
        if (attempt > 1)
            ESP_LOGW(TAG, "Retrying job %llu, attempt %d", (unsigned long long) job->job_id, attempt);
        ok = download_and_flash(ws, job, mstring_get_cstr(host), mstring_get_cstr(token), &error);
    }

    if (ok)
        ESP_LOGI(TAG, "Job %llu done", (unsigned long long) job->job_id);
    else
        ESP_LOGE(TAG, "Job %llu failed: %s", (unsigned long long) job->job_id, error);

    bmcloud_ws_report_job(ws, job->job_id, ok ? BmChannelJobDone : BmChannelJobFailed, ok ? NULL : error);
    bmcloud_ws_set_state(ws, ok ? BmChannelStateOnline : BmChannelStateError);
}

static void network_bmcloud_task(void* pvParameters)
{
    (void) pvParameters;

    vTaskDelay(pdMS_TO_TICKS(CLOUD_STARTUP_DELAY_MS));

    s_ws = bmcloud_ws_start();

    // at-least-once delivery: the server replays pending jobs on reconnect,
    // so skip whatever already ran (the last report finishes it server-side)
    uint64_t last_job_id = 0;

    while (1)
    {
        // Re-read the config every cycle: host/token can be changed from the web UI
        // and take effect without a reboot
        mstring_t* host  = mstring_alloc();
        mstring_t* token = mstring_alloc();

        bool configured = nvs_config_get_cloud_host(host) == ESP_OK && nvs_config_get_cloud_token(token) == ESP_OK;

        if (!configured)
        {
            ESP_LOGW(TAG, "Cloud host/token are not set, waiting for configuration");
            bmcloud_ws_stop(s_ws);
            mstring_free(host);
            mstring_free(token);
            vTaskDelay(pdMS_TO_TICKS(CLOUD_UNCONFIGURED_MS));
            continue;
        }

        // Presence and job pushes; the client reconnects on its own
        bmcloud_ws_ensure(s_ws, host, token);

        BmChannelJobType job;
        if (bmcloud_ws_pop_job(s_ws, &job))
        {
            if (job.job_id == last_job_id)
            {
                ESP_LOGD(TAG, "Job %llu already ran, skipped", (unsigned long long) job.job_id);
            }
            else
            {
                last_job_id = job.job_id;
                run_job(s_ws, host, token, &job);
            }
            // no delay: pick up the next queued job right away
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(CLOUD_POLL_PERIOD_MS));
        }

        mstring_free(host);
        mstring_free(token);
    }
}

void network_bmcloud_init(void)
{
    // 8 KB stack: TLS handshake with the cert bundle needs headroom
    xTaskCreate(network_bmcloud_task, "network_bmcloud_task", 8192, NULL, 5, NULL);
}
