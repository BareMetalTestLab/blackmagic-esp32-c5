#include <esp_log.h>
#include <esp_http_server.h>
#include "m-string.h"
#include "nvs-config.h"
#include "platform.h"

#define TAG "network-http-settings"

/* NVS settings GET handler */
esp_err_t nvs_settings_get_handler(httpd_req_t *req)
{
    mstring_t *hostname = mstring_alloc();
    nvs_config_get_hostname(hostname);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"hostname\":\"%s\"}", mstring_get_cstr(hostname));

    mstring_free(hostname);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/* NVS settings POST handler */
esp_err_t nvs_settings_post_handler(httpd_req_t *req)
{
    char content[512];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse URL-encoded form data
    char hostname_str[64] = {0};

    bool has_updates = false;
    mstring_t *hostname = mstring_alloc();

    if (httpd_query_key_value(content, "hostname", hostname_str, sizeof(hostname_str)) == ESP_OK)
    {
        mstring_set(hostname, hostname_str);
        nvs_config_set_hostname(hostname);
        has_updates = true;
        ESP_LOGI(TAG, "Hostname updated: %s", hostname_str);
    }

    mstring_free(hostname);

    httpd_resp_set_type(req, "application/json");
    if (has_updates)
    {
        httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send(req, "{\"success\":false,\"error\":\"No valid parameters\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}

/* Pins GET handler */
esp_err_t pins_get_handler(httpd_req_t *req)
{
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"swdio\":%ld,\"swclk\":%ld,\"tdi\":%ld,\"tdo\":%ld,\"trst\":%ld}",
             (long)g_pin_swdio, (long)g_pin_swclk,
             (long)g_pin_tdi, (long)g_pin_tdo, (long)g_pin_trst);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Pins POST handler */
esp_err_t pins_post_handler(httpd_req_t *req)
{
    char content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char val[8];
    int32_t swdio = g_pin_swdio, swclk = g_pin_swclk;
    int32_t tdi = g_pin_tdi, tdo = g_pin_tdo, trst = g_pin_trst;

    if (httpd_query_key_value(content, "swdio", val, sizeof(val)) == ESP_OK)
        swdio = (int32_t)atoi(val);
    if (httpd_query_key_value(content, "swclk", val, sizeof(val)) == ESP_OK)
        swclk = (int32_t)atoi(val);
    if (httpd_query_key_value(content, "tdi", val, sizeof(val)) == ESP_OK)
        tdi = (int32_t)atoi(val);
    if (httpd_query_key_value(content, "tdo", val, sizeof(val)) == ESP_OK)
        tdo = (int32_t)atoi(val);
    if (httpd_query_key_value(content, "trst", val, sizeof(val)) == ESP_OK)
        trst = (int32_t)atoi(val);

    nvs_config_set_pins(swdio, swclk, tdi, tdo, trst);

    // Apply immediately
    g_pin_swdio = swdio;
    g_pin_swclk = swclk;
    g_pin_tdi = tdi;
    g_pin_tdo = tdo;
    g_pin_trst = trst;

    ESP_LOGI(TAG, "Pins updated: SWDIO=%ld SWCLK=%ld TDI=%ld TDO=%ld TRST=%ld",
             (long)swdio, (long)swclk, (long)tdi, (long)tdo, (long)trst);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* --- Known-networks API --- */

static size_t json_escape(char *dst, size_t dst_max, const char *src)
{
    size_t pos = 0;
    while (*src && pos + 2 < dst_max)
    {
        char c = *src++;
        if (c == '"' || c == '\\')
        {
            if (pos + 3 >= dst_max)
                break;
            dst[pos++] = '\\';
        }
        dst[pos++] = c;
    }
    dst[pos] = '\0';
    return pos;
}

/* GET /networks  — returns [{idx,ssid,auto_connect}, ...] (no passwords) */
esp_err_t networks_get_handler(httpd_req_t *req)
{
    int32_t count = 0;
    nvs_config_get_networks_count(&count);

    size_t buf_size = 64 + (size_t)count * 120;
    char *resp = malloc(buf_size);
    if (!resp)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int pos = snprintf(resp, buf_size, "[");
    for (int32_t i = 0; i < count; i++)
    {
        wifi_network_t net;
        nvs_config_get_network(i, &net);

        char escaped_ssid[72];
        json_escape(escaped_ssid, sizeof(escaped_ssid), net.ssid);

        if (i > 0)
            pos += snprintf(resp + pos, buf_size - pos, ",");
        pos += snprintf(resp + pos, buf_size - pos,
                        "{\"idx\":%ld,\"ssid\":\"%s\",\"auto_connect\":%s}",
                        (long)i, escaped_ssid,
                        net.auto_connect ? "true" : "false");
    }
    pos += snprintf(resp + pos, buf_size - pos, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, pos);
    free(resp);
    return ESP_OK;
}

/* POST /networks/add  — body: ssid=...&pass=...&auto_connect=1 */
esp_err_t networks_add_handler(httpd_req_t *req)
{
    char content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char ssid_str[33] = {0};
    char pass_str[65] = {0};
    char auto_str[8] = {0};

    if (httpd_query_key_value(content, "ssid", ssid_str, sizeof(ssid_str)) != ESP_OK || ssid_str[0] == '\0')
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Missing SSID\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    httpd_query_key_value(content, "pass", pass_str, sizeof(pass_str));
    httpd_query_key_value(content, "auto_connect", auto_str, sizeof(auto_str));

    wifi_network_t net;
    strncpy(net.ssid, ssid_str, sizeof(net.ssid) - 1);
    net.ssid[sizeof(net.ssid) - 1] = '\0';
    strncpy(net.pass, pass_str, sizeof(net.pass) - 1);
    net.pass[sizeof(net.pass) - 1] = '\0';
    net.auto_connect = (strcmp(auto_str, "1") == 0 || strcmp(auto_str, "true") == 0);

    esp_err_t err = nvs_config_add_network(&net);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK)
    {
        httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else if (err == ESP_ERR_NO_MEM)
    {
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Maximum networks reached\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    else
    {
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to save\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}

/* POST /networks/update  — body: idx=0&ssid=...&pass=...&auto_connect=1 */
esp_err_t networks_update_handler(httpd_req_t *req)
{
    char content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char idx_str[8] = {0};
    if (httpd_query_key_value(content, "idx", idx_str, sizeof(idx_str)) != ESP_OK)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Missing index\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    int32_t idx = (int32_t)atoi(idx_str);
    int32_t count = 0;
    nvs_config_get_networks_count(&count);
    if (idx < 0 || idx >= count)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid index\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    /* Load existing network so unchanged fields are preserved */
    wifi_network_t net;
    nvs_config_get_network(idx, &net);

    char ssid_str[33] = {0};
    char pass_str[65] = {0};
    char auto_str[8] = {0};

    if (httpd_query_key_value(content, "ssid", ssid_str, sizeof(ssid_str)) == ESP_OK && ssid_str[0] != '\0')
    {
        strncpy(net.ssid, ssid_str, sizeof(net.ssid) - 1);
        net.ssid[sizeof(net.ssid) - 1] = '\0';
    }
    /* Only update password when a non-empty value is supplied */
    if (httpd_query_key_value(content, "pass", pass_str, sizeof(pass_str)) == ESP_OK && pass_str[0] != '\0')
    {
        strncpy(net.pass, pass_str, sizeof(net.pass) - 1);
        net.pass[sizeof(net.pass) - 1] = '\0';
    }
    if (httpd_query_key_value(content, "auto_connect", auto_str, sizeof(auto_str)) == ESP_OK)
    {
        net.auto_connect = (strcmp(auto_str, "1") == 0 || strcmp(auto_str, "true") == 0);
    }

    nvs_config_set_network(idx, &net);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* POST /networks/delete  — body: idx=0 */
esp_err_t networks_delete_handler(httpd_req_t *req)
{
    char content[64];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char idx_str[8] = {0};
    if (httpd_query_key_value(content, "idx", idx_str, sizeof(idx_str)) != ESP_OK)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Missing index\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    int32_t idx = (int32_t)atoi(idx_str);
    esp_err_t err = nvs_config_delete_network(idx);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK)
    {
        httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid index\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}
