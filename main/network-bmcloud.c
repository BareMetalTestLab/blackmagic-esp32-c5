#include "network-bmcloud.h"
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
    CloudResultOk,           // request succeeded
    CloudResultNetworkError, // server unreachable, timeout, connection dropped
    CloudResultAuthError,    // HTTP 401/403, token rejected
    CloudResultServerError,  // any other unexpected status or malformed response
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

static CloudDeviceStateType device_state_from_string(const char* state)
{
    if (strcmp(state, "online") == 0)
        return CloudDeviceStateOnline;
    if (strcmp(state, "busy") == 0)
        return CloudDeviceStateBusy;
    if (strcmp(state, "error") == 0)
        return CloudDeviceStateError;
    return CloudDeviceStateOffline;
}

static bool parse_device(const char* body, CloudDeviceType* device)
{
    cJSON* root = cJSON_Parse(body);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return false;
    }

    const cJSON* id         = cJSON_GetObjectItem(root, "id");
    const cJSON* name       = cJSON_GetObjectItem(root, "name");
    const cJSON* state      = cJSON_GetObjectItem(root, "state");
    const cJSON* updated_at = cJSON_GetObjectItem(root, "updated_at");

    bool ok = cJSON_IsNumber(id) && cJSON_IsString(name) && cJSON_IsString(state) && cJSON_IsNumber(updated_at);
    if (ok)
    {
        // valueint is a 32-bit int, valuedouble keeps the full i64 range
        device->id         = (int64_t) id->valuedouble;
        device->updated_at = (int64_t) updated_at->valuedouble;
        strlcpy(device->name, name->valuestring, sizeof(device->name));
        device->state = device_state_from_string(state->valuestring);
    }

    cJSON_Delete(root);
    return ok;
}

static CloudResultType get_me(const char* host, const char* token, CloudDeviceType* device)
{
    char url[256];

    build_url(url, sizeof(url), host, "/device/me");

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
    char buffer[RESPONSE_BUFFER_SIZE];

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        result = CloudResultNetworkError;
    }
    else
    {
        int content_length = esp_http_client_fetch_headers(client);
        int status         = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET /device/me: status = %d, content_length = %d", status, content_length);

        if (status == 401 /* Unauthorized */ || status == 403 /* Forbidden */)
        {
            result = CloudResultAuthError;
        }
        else if (status == HttpStatus_Ok && content_length >= 0 && content_length < (int) sizeof(buffer))
        {
            int total_read = 0;
            while (total_read < content_length)
            {
                int data_read = esp_http_client_read(client, buffer + total_read, content_length - total_read);
                if (data_read <= 0)
                {
                    result = CloudResultNetworkError;
                    break;
                }
                total_read += data_read;
            }
            buffer[total_read] = '\0';

            if (total_read == content_length && parse_device(buffer, device))
            {
                ESP_LOGI(TAG,
                         "Device %lld '%s' state = %d, updated_at = %lld",
                         (long long) device->id,
                         device->name,
                         (int) device->state,
                         (long long) device->updated_at);
                result = CloudResultOk;
            }
            else if (result != CloudResultNetworkError)
            {
                ESP_LOGE(TAG, "Failed to parse device response");
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
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

#define CLOUD_STARTUP_DELAY_MS  (5 * 1000)  // give the network time to come up after boot
#define CLOUD_POLL_PERIOD_MS    (10 * 1000) // ping interval while everything is fine
#define CLOUD_RETRY_MIN_MS      (5 * 1000)  // first retry after a failure
#define CLOUD_RETRY_MAX_MS      (60 * 1000) // retry backoff cap
#define CLOUD_AUTH_RETRY_MS     (30 * 1000) // re-check rate while the token is rejected
#define CLOUD_UNCONFIGURED_MS   (10 * 1000) // re-read NVS while host/token are not set
#define FIRMWARE_CHUNK_SIZE     (1024)      // download read chunk
#define FIRMWARE_MAX_ATTEMPTS   (3)         // download retries before reporting a failed job

typedef struct
{
    int64_t job_id;
    int64_t firmware_id;
} CloudJobType;

/* GET /device/job: claim the oldest pending job, it atomically moves to flashing.
 * Returns false on 404 (no pending jobs) or on any failure. */
static bool claim_job(const char* host, const char* token, CloudJobType* job)
{
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
        return false;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    bool claimed = false;
    char buffer[RESPONSE_BUFFER_SIZE];

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK)
    {
        int content_length = esp_http_client_fetch_headers(client);
        int status         = esp_http_client_get_status_code(client);

        if (status == 404 /* No pending jobs */)
        {
            ESP_LOGD(TAG, "No pending jobs");
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

            cJSON* root = cJSON_Parse(buffer);
            const cJSON* job_id      = cJSON_GetObjectItem(root, "job_id");
            const cJSON* firmware_id = cJSON_GetObjectItem(root, "firmware_id");
            if (cJSON_IsNumber(job_id) && cJSON_IsNumber(firmware_id))
            {
                job->job_id      = (int64_t) job_id->valuedouble;
                job->firmware_id = (int64_t) firmware_id->valuedouble;
                claimed          = true;
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
    else
    {
        ESP_LOGE(TAG, "HTTP GET /device/job failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return claimed;
}

/* GET /device/firmware/{id}: stream the firmware and count the bytes.
 * TODO: this is a stub — the bytes are discarded, actual flashing is not wired up yet.
 * Returns the number of bytes received or -1 on failure. */
static int download_firmware(const char* host, const char* token, int64_t firmware_id)
{
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
        return -1;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    int  total_read = -1;
    char buffer[FIRMWARE_CHUNK_SIZE];

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK)
    {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        if (status == HttpStatus_Ok)
        {
            total_read = 0;
            // Read until EOF: works for both content-length and chunked responses
            while (1)
            {
                int data_read = esp_http_client_read(client, buffer, sizeof(buffer));
                if (data_read < 0)
                {
                    ESP_LOGE(TAG, "Firmware download interrupted after %d bytes", total_read);
                    total_read = -1;
                    break;
                }
                if (data_read == 0)
                    break;
                total_read += data_read;
            }
        }
        else
        {
            ESP_LOGE(TAG, "HTTP GET firmware: unexpected status = %d", status);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET firmware failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return total_read;
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

/* Claim jobs one by one until the queue is empty and run them */
static void check_jobs(const char* host, const char* token)
{
    CloudJobType job;
    while (claim_job(host, token, &job))
    {
        ESP_LOGI(TAG, "Claimed job %lld, firmware %lld", (long long) job.job_id, (long long) job.firmware_id);
        send_state(host, token, CloudDeviceStateBusy);

        int size    = -1;
        int attempt = 1;
        while (attempt <= FIRMWARE_MAX_ATTEMPTS && size < 0)
        {
            if (attempt > 1)
                ESP_LOGW(TAG, "Retrying firmware download, attempt %d", attempt);
            size = download_firmware(host, token, job.firmware_id);
            attempt++;
        }

        bool ok = (size >= 0);
        if (ok)
        {
            ESP_LOGI(TAG, "Downloaded %d firmware bytes, flashing is not implemented yet", size);
        }

        report_job(host, token, job.job_id, ok, "firmware download failed");
        send_state(host, token, CloudDeviceStateOnline);
    }
}

static void network_bmcloud_task(void* pvParameters)
{
    (void) pvParameters;

    vTaskDelay(pdMS_TO_TICKS(CLOUD_STARTUP_DELAY_MS));

    uint32_t retry_delay_ms = CLOUD_RETRY_MIN_MS;

    while (1)
    {
        // Re-read the config every cycle: host/token can be changed from the web UI
        // and take effect without a reboot
        mstring_t* host  = mstring_alloc();
        mstring_t* token = mstring_alloc();

        bool configured = nvs_config_get_cloud_host(host) == ESP_OK
            && nvs_config_get_cloud_token(token) == ESP_OK;

        if (!configured)
        {
            ESP_LOGW(TAG, "Cloud host/token are not set, waiting for configuration");
            mstring_free(host);
            mstring_free(token);
            vTaskDelay(pdMS_TO_TICKS(CLOUD_UNCONFIGURED_MS));
            continue;
        }

        // GET /device/me doubles as the ping: it checks both reachability and the token
        CloudDeviceType device;
        CloudResultType result = get_me(mstring_get_cstr(host), mstring_get_cstr(token), &device);

        uint32_t wait_ms;
        if (result == CloudResultOk)
        {
            retry_delay_ms = CLOUD_RETRY_MIN_MS;
            check_jobs(mstring_get_cstr(host), mstring_get_cstr(token));
            wait_ms = CLOUD_POLL_PERIOD_MS;
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
            ESP_LOGW(TAG,
                     "Cloud unreachable, next attempt in %lu s",
                     (unsigned long) (retry_delay_ms / 1000));
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
