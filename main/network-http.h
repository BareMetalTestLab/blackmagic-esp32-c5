#pragma once
#include <stdint.h>
#include <stddef.h>
#include <esp_http_server.h>

void network_http_server_init(void);

/* Common answer for successful mutations: 204 No Content */
static inline esp_err_t httpd_resp_no_content(httpd_req_t* req)
{
    return httpd_resp_set_status(req, "204 No Content") == ESP_OK
               ? httpd_resp_send(req, NULL, 0)
               : ESP_FAIL;
}
