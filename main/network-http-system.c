#include <esp_log.h>
#include <esp_http_server.h>

#define TAG "network-http-system"

/* Reboot handler */
esp_err_t reboot_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reboot request received");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    
    // Delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Rebooting device...");
    esp_restart();
    
    return ESP_OK;
}
