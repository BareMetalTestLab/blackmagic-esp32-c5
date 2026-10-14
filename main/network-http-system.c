#include <esp_log.h>
#include <esp_http_server.h>
#include "network-http.h"

#define TAG "network-http-system"

/* Reboot handler */
esp_err_t reboot_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reboot request received");

    httpd_resp_no_content(req);
    
    // Delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Rebooting device...");
    esp_restart();
    
    return ESP_OK;
}
