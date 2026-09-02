#include <network-http.h>
#include <esp_http_server.h>
#include "esp_event.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <sys/param.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "network-http-page.h"

#define TAG "network-http"

esp_err_t connection_params_post_handler(httpd_req_t *req);
esp_err_t upload_post_handler(httpd_req_t *req);
esp_err_t erase_post_handler(httpd_req_t *req);

esp_err_t nvs_settings_post_handler(httpd_req_t *req);
esp_err_t nvs_settings_get_handler(httpd_req_t *req);

esp_err_t reboot_post_handler(httpd_req_t *req);

esp_err_t pins_post_handler(httpd_req_t *req);
esp_err_t pins_default_post_handler(httpd_req_t *req);
esp_err_t pins_get_handler(httpd_req_t *req);

esp_err_t networks_get_handler(httpd_req_t *req);
esp_err_t networks_add_handler(httpd_req_t *req);
esp_err_t networks_update_handler(httpd_req_t *req);
esp_err_t networks_delete_handler(httpd_req_t *req);

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/* Favicon handler to suppress 404 warnings */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler};

static const httpd_uri_t upload = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler};

static const httpd_uri_t erase = {
    .uri = "/erase",
    .method = HTTP_POST,
    .handler = erase_post_handler};

static const httpd_uri_t connection_params_uri = {
    .uri = "/connection-params",
    .method = HTTP_POST,
    .handler = connection_params_post_handler};

static const httpd_uri_t nvs_settings_get_uri = {
    .uri = "/nvs-settings",
    .method = HTTP_GET,
    .handler = nvs_settings_get_handler};

static const httpd_uri_t nvs_settings_post_uri = {
    .uri = "/nvs-settings",
    .method = HTTP_POST,
    .handler = nvs_settings_post_handler};

static const httpd_uri_t favicon_uri = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler};

static const httpd_uri_t reboot_uri = {
    .uri = "/reboot",
    .method = HTTP_POST,
    .handler = reboot_post_handler};

static const httpd_uri_t pins_get_uri = {
    .uri = "/pins",
    .method = HTTP_GET,
    .handler = pins_get_handler};

static const httpd_uri_t pins_post_uri = {
    .uri = "/pins",
    .method = HTTP_POST,
    .handler = pins_post_handler};

static const httpd_uri_t pins_default_post_uri = {
    .uri = "/pins-default",
    .method = HTTP_POST,
    .handler = pins_default_post_handler};

static const httpd_uri_t networks_get_uri = {
    .uri = "/networks",
    .method = HTTP_GET,
    .handler = networks_get_handler};

static const httpd_uri_t networks_add_uri = {
    .uri = "/networks/add",
    .method = HTTP_POST,
    .handler = networks_add_handler};

static const httpd_uri_t networks_update_uri = {
    .uri = "/networks/update",
    .method = HTTP_POST,
    .handler = networks_update_handler};

static const httpd_uri_t networks_delete_uri = {
    .uri = "/networks/delete",
    .method = HTTP_POST,
    .handler = networks_delete_handler};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    conf.max_uri_handlers = 16;

    esp_err_t ret = httpd_start(&server, &conf);
    if (ESP_OK != ret)
    {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &upload);
    httpd_register_uri_handler(server, &erase);
    httpd_register_uri_handler(server, &connection_params_uri);
    httpd_register_uri_handler(server, &nvs_settings_get_uri);
    httpd_register_uri_handler(server, &nvs_settings_post_uri);
    httpd_register_uri_handler(server, &favicon_uri);
    httpd_register_uri_handler(server, &reboot_uri);
    httpd_register_uri_handler(server, &pins_get_uri);
    httpd_register_uri_handler(server, &pins_post_uri);
    httpd_register_uri_handler(server, &pins_default_post_uri);
    httpd_register_uri_handler(server, &networks_get_uri);
    httpd_register_uri_handler(server, &networks_add_uri);
    httpd_register_uri_handler(server, &networks_update_uri);
    httpd_register_uri_handler(server, &networks_delete_uri);
    return server;
}

void network_http_server_init(void)
{
    ESP_LOGI(TAG, "init http server");

    start_webserver();

    ESP_LOGI(TAG, "init rest server done");
}
