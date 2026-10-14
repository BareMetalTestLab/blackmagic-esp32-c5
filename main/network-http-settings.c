#include <esp_log.h>
#include <esp_http_server.h>
#include "esp_err.h"
#include "m-string.h"
#include "network-http.h"
#include "nvs-config.h"
#include "platform.h"

#define TAG "network-http-settings"

/* NVS settings GET handler */
esp_err_t settings_mdns_get_handler(httpd_req_t* req)
{
    mstring_t* mdns_name = mstring_alloc();
    nvs_config_get_mdns_name(mdns_name);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"mdns_name\":\"%s\"}", mstring_get_cstr(mdns_name));

    mstring_free(mdns_name);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/* NVS settings POST handler */
esp_err_t settings_mdns_post_handler(httpd_req_t* req)
{
    char   content[512];
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
    char mdns_name_str[64] = {0};

    if (httpd_query_key_value(content, "mdns_name", mdns_name_str, sizeof(mdns_name_str)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad mDNS name");
        return ESP_FAIL;
    }

    mstring_t* mdns_name = mstring_alloc_set(mdns_name_str);
    nvs_config_set_mdns_name(mdns_name);
    mstring_free(mdns_name);

    httpd_resp_no_content(req);
    ESP_LOGI(TAG, "mDNS name updated: %s", mdns_name_str);
    return ESP_OK;
}

/* GET /settings/pins */
esp_err_t settings_pins_get_handler(httpd_req_t* req)
{
    char resp[256];
    snprintf(resp,
             sizeof(resp),
             "{\"swdio\":%ld,\"swclk\":%ld,\"tdi\":%ld,\"tdo\":%ld,\"trst\":%ld}",
             (long) g_pin_tms_swdio,
             (long) g_pin_tck_swclk,
             (long) g_pin_tdi,
             (long) g_pin_tdo_swo,
             (long) g_pin_trst);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* POST /settings/pins  — body: swdio=..&swclk=..&tdi=..&tdo=..&trst=.. */
esp_err_t settings_pins_post_handler(httpd_req_t* req)
{
    char   content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char    val[8];
    int32_t swdio = g_pin_tms_swdio, swclk = g_pin_tck_swclk;
    int32_t tdi = g_pin_tdi, tdo = g_pin_tdo_swo, trst = g_pin_trst;

    if (httpd_query_key_value(content, "swdio", val, sizeof(val)) == ESP_OK)
        swdio = (int32_t) atoi(val);
    if (httpd_query_key_value(content, "swclk", val, sizeof(val)) == ESP_OK)
        swclk = (int32_t) atoi(val);
    if (httpd_query_key_value(content, "tdi", val, sizeof(val)) == ESP_OK)
        tdi = (int32_t) atoi(val);
    if (httpd_query_key_value(content, "tdo", val, sizeof(val)) == ESP_OK)
        tdo = (int32_t) atoi(val);
    if (httpd_query_key_value(content, "trst", val, sizeof(val)) == ESP_OK)
        trst = (int32_t) atoi(val);

    // Apply immediately
    g_pin_tms_swdio = swdio ? swdio : DEFAULT_TMS_PIN;
    g_pin_tck_swclk = swclk ? swclk : DEFAULT_TCK_PIN;
    g_pin_tdi       = tdi ? tdi : DEFAULT_TDI_PIN;
    g_pin_tdo_swo   = tdo ? tdo : DEFAULT_TDO_PIN;
    g_pin_trst      = trst ? trst : DEFAULT_TRST_PIN;

    nvs_config_set_pins(g_pin_tms_swdio, g_pin_tck_swclk, g_pin_tdi, g_pin_tdo_swo, g_pin_trst);

    ESP_LOGI(TAG,
             "Pins updated: SWDIO=%ld SWCLK=%ld TDI=%ld TDO=%ld TRST=%ld",
             (long) g_pin_tms_swdio,
             (long) g_pin_tck_swclk,
             (long) g_pin_tdi,
             (long) g_pin_tdo_swo,
             (long) g_pin_trst);

    httpd_resp_no_content(req);
    return ESP_OK;
}

/* --- Known-networks API --- */

static size_t json_escape(char* dst, size_t dst_max, const char* src)
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

/* GET /settings/networks  — returns [{idx,ssid,auto_connect}, ...] (no passwords) */
esp_err_t settings_networks_get_handler(httpd_req_t* req)
{
    int32_t count = 0;
    nvs_config_get_networks_count(&count);

    size_t buf_size = 64 + (size_t) count * 120;
    char*  resp     = malloc(buf_size);
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
        pos += snprintf(resp + pos,
                        buf_size - pos,
                        "{\"idx\":%ld,\"ssid\":\"%s\",\"auto_connect\":%s}",
                        (long) i,
                        escaped_ssid,
                        net.auto_connect ? "true" : "false");
    }
    pos += snprintf(resp + pos, buf_size - pos, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, pos);
    free(resp);
    return ESP_OK;
}

/* POST /settings/networks  — body: ssid=...&pass=...&auto_connect=1 */
esp_err_t settings_networks_post_handler(httpd_req_t* req)
{
    char   content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int    ret       = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char ssid_str[33] = {0};
    char pass_str[65] = {0};
    char auto_str[8]  = {0};

    if (httpd_query_key_value(content, "ssid", ssid_str, sizeof(ssid_str)) != ESP_OK || ssid_str[0] == '\0')
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    httpd_query_key_value(content, "pass", pass_str, sizeof(pass_str));
    httpd_query_key_value(content, "auto_connect", auto_str, sizeof(auto_str));

    wifi_network_t net;
    strncpy(net.ssid, ssid_str, sizeof(net.ssid) - 1);
    net.ssid[sizeof(net.ssid) - 1] = '\0';
    strncpy(net.pass, pass_str, sizeof(net.pass) - 1);
    net.pass[sizeof(net.pass) - 1] = '\0';
    net.auto_connect               = (strcmp(auto_str, "1") == 0 || strcmp(auto_str, "true") == 0);

    esp_err_t err = nvs_config_add_network(&net);

    if (err == ESP_OK)
    {
        httpd_resp_no_content(req);
        return ESP_OK;
    }
    else if (err == ESP_ERR_NO_MEM)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Maximum networks reached");
        return ESP_FAIL;
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save network");
        return ESP_FAIL;
    }
}

/* PATCH /settings/networks  — body: idx=0&ssid=...&pass=...&auto_connect=1 */
esp_err_t settings_networks_patch_handler(httpd_req_t* req)
{
    char   content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int    ret       = httpd_req_recv(req, content, recv_size);
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing index");
        return ESP_FAIL;
    }

    int32_t idx   = (int32_t) atoi(idx_str);
    int32_t count = 0;
    nvs_config_get_networks_count(&count);
    if (idx < 0 || idx >= count)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }

    /* Load existing network so unchanged fields are preserved */
    wifi_network_t net;
    nvs_config_get_network(idx, &net);

    char ssid_str[33] = {0};
    char pass_str[65] = {0};
    char auto_str[8]  = {0};

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

    httpd_resp_no_content(req);
    return ESP_OK;
}

/* DELETE /settings/networks  — body: idx=0 */
esp_err_t settings_networks_delete_handler(httpd_req_t* req)
{
    char   content[64];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int    ret       = httpd_req_recv(req, content, recv_size);
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing index");
        return ESP_FAIL;
    }

    int32_t   idx = (int32_t) atoi(idx_str);
    esp_err_t err = nvs_config_delete_network(idx);

    if (err == ESP_OK)
    {
        httpd_resp_no_content(req);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }
}

esp_err_t settings_cloud_get_handler(httpd_req_t* req)
{
    mstring_t* cloud_host  = mstring_alloc();
    mstring_t* cloud_token = mstring_alloc();
    nvs_config_get_cloud_host(cloud_host);
    // nvs_config_get_cloud_token(cloud_token);

    char resp[192];
    snprintf(resp,
             sizeof(resp),
             "{\"cloud_host\":\"%s\",\"cloud_token\":\"%s\"}",
             mstring_get_cstr(cloud_host),
             mstring_get_cstr(cloud_token));

    mstring_free(cloud_host);
    mstring_free(cloud_token);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t settings_cloud_post_handler(httpd_req_t* req)
{
    char   content[512];
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
    char cloud_host_str[64]  = {0};
    char cloud_token_str[64] = {0};

    if ((httpd_query_key_value(content, "cloud_host", cloud_host_str, sizeof(cloud_host_str)) != ESP_OK) ||
        (httpd_query_key_value(content, "cloud_token", cloud_token_str, sizeof(cloud_token_str)) != ESP_OK))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    mstring_t* cloud_host  = mstring_alloc_set(cloud_host_str);
    mstring_t* cloud_token = mstring_alloc_set(cloud_token_str);
    nvs_config_set_cloud_host(cloud_host);
    nvs_config_set_cloud_token(cloud_token);
    mstring_free(cloud_host);
    mstring_free(cloud_token);
    ESP_LOGI(TAG, "Cloud token updated: %s", cloud_token_str);

    httpd_resp_no_content(req);
    return ESP_OK;
}
