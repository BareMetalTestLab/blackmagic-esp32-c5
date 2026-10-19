#include "network-bmcloud.h"
#include "just_flasher.h"
#include "freertos/idf_additions.h"
#include "m-string.h"
#include "nvs-config.h"
#include <cJSON.h>
#include <ctype.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

#define RESPONSE_BUFFER_SIZE 512

/* Outcome of a cloud request, the polling task picks a retry strategy based on it */
typedef enum
{
    CloudResultOk,            // request succeeded
    CloudResultNetworkError,  // server unreachable, timeout, connection dropped
    CloudResultAuthError,     // HTTP 401/403, token rejected
    CloudResultServerError,   // any other unexpected status or malformed response
} CloudResultType;

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

static CloudResultType send_state(const char* host, const char* token, CloudDeviceStateType state)
{
    char url[256];
    build_url(url, sizeof(url), host, "/device/state");

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return CloudResultNetworkError;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    const char* states[] = {
        [CloudDeviceStateOffline] = "offline",
        [CloudDeviceStateOnline]  = "online",
        [CloudDeviceStateError]   = "error",
        [CloudDeviceStateBusy]    = "busy",
    };

    char payload[32];
    int  payload_len = snprintf(payload, sizeof(payload), "{\"state\":\"%s\"}", states[state]);

    CloudResultType result = CloudResultServerError;
    esp_err_t       err    = esp_http_client_open(client, payload_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP POST /device/state failed: %s", esp_err_to_name(err));
        result = CloudResultNetworkError;
    }
    else if (esp_http_client_write(client, payload, payload_len) == payload_len)
    {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST /device/state: status = %d", status);
        if (status == 204 /* HttpStatus_NoContent is not in IDF's enum */)
        {
            result = CloudResultOk;
        }
        else if (status == 401 /* Unauthorized */ || status == 403 /* Forbidden */)
        {
            result = CloudResultAuthError;
        }
    }

    if (result != CloudResultOk)
    {
        ESP_LOGE(TAG, "Failed to update device state");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

void network_bmcloud_set_status(CloudDeviceStateType state)
{
    mstring_t* host  = mstring_alloc();
    mstring_t* token = mstring_alloc();

    if (nvs_config_get_cloud_host(host) == ESP_OK && nvs_config_get_cloud_token(token) == ESP_OK)
    {
        send_state(mstring_get_cstr(host), mstring_get_cstr(token), state);
    }

    mstring_free(host);
    mstring_free(token);
}

/* --- Cloud polling automat --- */

#define CLOUD_STARTUP_DELAY_MS     (5 * 1000)        // give the network time to come up after boot
#define CLOUD_POLL_PERIOD_MS       (10 * 1000)       // ping interval while everything is fine
#define CLOUD_RETRY_MIN_MS         (5 * 1000)        // first retry after a failure
#define CLOUD_RETRY_MAX_MS         (60 * 1000)       // retry backoff cap
#define CLOUD_AUTH_RETRY_MS        (30 * 1000)       // re-check rate while the token is rejected
#define CLOUD_UNCONFIGURED_MS      (10 * 1000)       // re-read NVS while host/token are not set
#define FIRMWARE_CHUNK_SIZE        (1024)            // download read chunk
#define FIRMWARE_MAX_ATTEMPTS      (3)               // download retries before reporting a failed job
#define CLOUD_FLASHER_LOCK_WAIT_MS (10 * 60 * 1000)  // wait out a concurrent web flash before failing the job

typedef struct
{
    int64_t job_id;
    int64_t firmware_id;
} CloudJobType;

/* GET /device/job: claim the oldest pending job, it atomically moves to flashing.
 * Doubles as the connectivity check: 404 means reachable, authenticated,
 * there is just no work to do. */
static CloudResultType claim_job(const char* host, const char* token, CloudJobType* job, bool* has_job)
{
    *has_job = false;

    char url[256];
    build_url(url, sizeof(url), host, "/device/job");

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
        return CloudResultNetworkError;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    CloudResultType result = CloudResultServerError;
    char            buffer[RESPONSE_BUFFER_SIZE];

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET /device/job failed: %s", esp_err_to_name(err));
        result = CloudResultNetworkError;
    }
    else
    {
        int content_length = esp_http_client_fetch_headers(client);
        int status         = esp_http_client_get_status_code(client);

        if (status == 401 /* Unauthorized */ || status == 403 /* Forbidden */)
        {
            result = CloudResultAuthError;
        }
        else if (status == 404 /* No pending jobs */)
        {
            ESP_LOGD(TAG, "No pending jobs");
            result = CloudResultOk;
        }
        else if (status == HttpStatus_Ok && content_length >= 0 && content_length < (int) sizeof(buffer))
        {
            int total_read = 0;
            while (total_read < content_length)
            {
                int data_read = esp_http_client_read(client, buffer + total_read, content_length - total_read);
                if (data_read <= 0)
                    break;
                total_read += data_read;
            }
            buffer[total_read] = '\0';

            cJSON*       root        = cJSON_Parse(buffer);
            const cJSON* job_id      = cJSON_GetObjectItem(root, "job_id");
            const cJSON* firmware_id = cJSON_GetObjectItem(root, "firmware_id");
            if (total_read == content_length && cJSON_IsNumber(job_id) && cJSON_IsNumber(firmware_id))
            {
                job->job_id      = (int64_t) job_id->valuedouble;
                job->firmware_id = (int64_t) firmware_id->valuedouble;
                *has_job         = true;
                result           = CloudResultOk;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to parse job response");
            }
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGE(TAG, "HTTP GET /device/job: unexpected status = %d", status);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

/* GET /device/firmware/{id}: download the firmware and stream it into the
 * target flash. On failure *error points to a short reason for the job report. */
static bool download_and_flash(const char* host, const char* token, int64_t firmware_id, const char** error)
{
    *error = "firmware download failed";

    char path[64];
    snprintf(path, sizeof(path), "/device/firmware/%lld", (long long) firmware_id);

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
        // Erase exactly what we are about to download, or the whole main bank
        // when the server does not report the size
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

            ferr = just_flasher_write(FLASHER_DEFAULT_BASE_ADDR + written, buffer, data_read);
            if (ferr != JustFlasherOk)
            {
                *error = just_flasher_err_str(ferr);
                just_flasher_abort();
                goto out;
            }
            written += data_read;

            // Log progress every 10% when the size is known
            if (content_length > 0)
            {
                int progress = (int) (written * 100 / (size_t) content_length);
                if (progress / 10 > last_progress_reported / 10)
                {
                    ESP_LOGI(TAG, "Flash progress: %zu / %d bytes (%d%%)", written, content_length, progress);
                    last_progress_reported = progress;
                }
            }
        }

        // Flush writes, reset the target and release it
        just_flasher_end();
        ESP_LOGI(TAG, "Flashed %zu firmware bytes", written);
        ok = true;
    }

out:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* POST /device/job/{job_id}: report the outcome of a claimed job */
static bool report_job(const char* host, const char* token, int64_t job_id, bool success, const char* error)
{
    char path[64];
    snprintf(path, sizeof(path), "/device/job/%lld", (long long) job_id);

    char url[256];
    build_url(url, sizeof(url), host, path);

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
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
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char payload[128];
    int  payload_len;
    if (success)
    {
        payload_len = snprintf(payload, sizeof(payload), "{\"status\":\"done\"}");
    }
    else
    {
        payload_len = snprintf(payload, sizeof(payload), "{\"status\":\"failed\",\"error\":\"%s\"}", error);
    }

    bool      ok  = false;
    esp_err_t err = esp_http_client_open(client, payload_len);
    if (err == ESP_OK)
    {
        if (esp_http_client_write(client, payload, payload_len) == payload_len)
        {
            esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP POST /device/job: status = %d", status);
            ok = (status == 204 /* HttpStatus_NoContent is not in IDF's enum */);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST /device/job failed: %s", esp_err_to_name(err));
    }

    if (!ok)
        ESP_LOGE(TAG, "Failed to report job result, the job stays flashing on the server");

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* Execute one claimed job: download the firmware, flash it and report the outcome */
static void run_job(const char* host, const char* token, const CloudJobType* job)
{
    ESP_LOGI(TAG, "Claimed job %lld, firmware %lld", (long long) job->job_id, (long long) job->firmware_id);
    send_state(host, token, CloudDeviceStateBusy);

    const char* error = "firmware download failed";
    bool        ok    = false;
    for (int attempt = 1; attempt <= FIRMWARE_MAX_ATTEMPTS && !ok; attempt++)
    {
        if (attempt > 1)
            ESP_LOGW(TAG, "Retrying job %lld, attempt %d", (long long) job->job_id, attempt);
        ok = download_and_flash(host, token, job->firmware_id, &error);
    }

    report_job(host, token, job->job_id, ok, error);
    send_state(host, token, ok ? CloudDeviceStateOnline : CloudDeviceStateError);
}

static void network_bmcloud_task(void* pvParameters)
{
    (void) pvParameters;

    vTaskDelay(pdMS_TO_TICKS(CLOUD_STARTUP_DELAY_MS));

    uint32_t retry_delay_ms = CLOUD_RETRY_MIN_MS;
    bool       online_sent     = false;      // online announced after the first successful contact
    mstring_t* announced_host  = mstring_alloc(); // server we have announced online to

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
            mstring_free(host);
            mstring_free(token);
            vTaskDelay(pdMS_TO_TICKS(CLOUD_UNCONFIGURED_MS));
            continue;
        }

        // Server changed in the settings: announce online to the new one again
        if (online_sent && mstring_cmp_cstr(announced_host, mstring_get_cstr(host)) != 0)
            online_sent = false;

        // GET /device/job is both the ping and the work source: it checks
        // reachability and the token, and claims a job if there is one
        CloudJobType   job;
        bool           has_job = false;
        CloudResultType result = claim_job(mstring_get_cstr(host), mstring_get_cstr(token), &job, &has_job);

        uint32_t wait_ms;
        if (result == CloudResultOk)
        {
            retry_delay_ms = CLOUD_RETRY_MIN_MS;
            if (!online_sent)
            {
                // Announce ourselves once after the first successful contact
                // with a server; a claimed job reports busy/online on its own
                if (!has_job)
                    send_state(mstring_get_cstr(host), mstring_get_cstr(token), CloudDeviceStateOnline);
                mstring_set(announced_host, mstring_get_cstr(host));
                online_sent = true;
            }
            if (has_job)
            {
                run_job(mstring_get_cstr(host), mstring_get_cstr(token), &job);
                wait_ms = 0; // pick up the next queued job right away
            }
            else
            {
                wait_ms = CLOUD_POLL_PERIOD_MS;
            }
        }
        else if (result == CloudResultAuthError)
        {
            // Retrying with the same token is pointless, but the token can be fixed
            // from the web UI, so keep re-checking it at a slow fixed rate
            ESP_LOGE(TAG, "Server rejected the token, check the cloud token in settings");
            wait_ms = CLOUD_AUTH_RETRY_MS;
        }
        else
        {
            // Unreachable or broken response: exponential backoff, so we don't hammer
            // the server (and the log) while it is down
            ESP_LOGW(TAG, "Cloud unreachable, next attempt in %lu s", (unsigned long) (retry_delay_ms / 1000));
            wait_ms = retry_delay_ms;
            retry_delay_ms *= 2;
            if (retry_delay_ms > CLOUD_RETRY_MAX_MS)
                retry_delay_ms = CLOUD_RETRY_MAX_MS;
        }

        mstring_free(host);
        mstring_free(token);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

void network_bmcloud_init(void)
{
    // 8 KB stack: TLS handshake with the cert bundle needs headroom
    xTaskCreate(network_bmcloud_task, "network_bmcloud_task", 8192, NULL, 5, NULL);
}
