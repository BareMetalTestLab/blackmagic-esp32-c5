#include "network-bmcloud.h"
#include "freertos/idf_additions.h"
#include "m-string.h"
#include "nvs-config.h"
#include <cJSON.h>
#include <esp_http_client.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#define TAG "cloud"

#define RESPONSE_BUFFER_SIZE 512

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

static bool get_me(const char* host, const char* token, CloudDeviceType* device)
{
    char url[256];

    snprintf(url, sizeof(url), "http://%s/device/me", host);

    esp_http_client_config_t config = {
        .url        = url,
        .method     = HTTP_METHOD_GET,
        .timeout_ms = 10000,
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
    char buffer[RESPONSE_BUFFER_SIZE];

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK)
    {
        int content_length = esp_http_client_fetch_headers(client);
        int status         = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET /device/me: status = %d, content_length = %d", status, content_length);

        if (status == HttpStatus_Ok && content_length >= 0 && content_length < (int) sizeof(buffer))
        {
            int total_read = 0;
            while (total_read < content_length)
            {
                int data_read = esp_http_client_read(client, buffer + total_read, content_length - total_read);
                if (data_read <= 0)
                {
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
                ok = true;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to parse device response");
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static bool send_state(const char* host, const char* token, CloudDeviceStateType state)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s/device/state", host);

    esp_http_client_config_t config = {
        .url        = url,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = 10000,
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

    const char* states[] = {
        [CloudDeviceStateOffline] = "offline",
        [CloudDeviceStateOnline]  = "online",
        [CloudDeviceStateError]   = "error",
        [CloudDeviceStateBusy]    = "busy",
    };

    char payload[32];
    int  payload_len = snprintf(payload, sizeof(payload), "{\"state\":\"%s\"}", states[state]);

    bool      ok  = false;
    esp_err_t err = esp_http_client_open(client, payload_len);
    if (err == ESP_OK)
    {
        if (esp_http_client_write(client, payload, payload_len) == payload_len)
        {
            esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP POST /device/state: status = %d", status);
            ok = (status == 204 /* HttpStatus_NoContent is not in IDF's enum */);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST /device/state failed: %s", esp_err_to_name(err));
    }

    if (!ok)
    {
        ESP_LOGE(TAG, "Failed to update device state");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool network_bmcloud_try_connect(CloudDeviceType* device)
{
    bool success = false;
    if (device == NULL)
        return success;

    mstring_t* host  = mstring_alloc();
    mstring_t* token = mstring_alloc();

    if (nvs_config_get_cloud_host(host) == ESP_OK && nvs_config_get_cloud_token(token) == ESP_OK)
    {
        get_me(mstring_get_cstr(host), mstring_get_cstr(token), device);
        success = true;
    }

    mstring_free(host);
    mstring_free(token);
    return true;
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
