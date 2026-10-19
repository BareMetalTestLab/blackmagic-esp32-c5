#include <network-http.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <sys/param.h>
#include <string.h>
#include "network-http-page.h"

#define TAG "network-http"

esp_err_t reboot_post_handler(httpd_req_t *req);

esp_err_t target_flash_upload_post_handler(httpd_req_t *req);
esp_err_t target_flash_erase_post_handler(httpd_req_t *req);
esp_err_t target_flash_read_post_handler(httpd_req_t *req);

esp_err_t settings_mdns_post_handler(httpd_req_t *req);
esp_err_t settings_mdns_get_handler(httpd_req_t *req);

esp_err_t settings_pins_get_handler(httpd_req_t *req);
esp_err_t settings_pins_post_handler(httpd_req_t *req);

esp_err_t settings_networks_get_handler(httpd_req_t *req);
esp_err_t settings_networks_post_handler(httpd_req_t *req);
esp_err_t settings_networks_patch_handler(httpd_req_t *req);
esp_err_t settings_networks_delete_handler(httpd_req_t *req);

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler};

static const httpd_uri_t reboot_uri = {
    .uri = "/reboot",
    .method = HTTP_POST,
    .handler = reboot_post_handler};

static const httpd_uri_t target_flash_upload_post_uri = {
    .uri = "/target/flash/upload",
    .method = HTTP_POST,
    .handler = target_flash_upload_post_handler};

static const httpd_uri_t target_flash_erase_post_uri = {
    .uri = "/target/flash/erase",
    .method = HTTP_POST,
    .handler = target_flash_erase_post_handler};

static const httpd_uri_t target_flash_read_post_uri = {
    .uri = "/target/flash/read",
    .method = HTTP_POST,
    .handler = target_flash_read_post_handler};

static const httpd_uri_t settings_mdns_get_uri = {
    .uri = "/settings/mdns",
    .method = HTTP_GET,
    .handler = settings_mdns_get_handler};

static const httpd_uri_t settings_mdns_post_uri = {
    .uri = "/settings/mdns",
    .method = HTTP_POST,
    .handler = settings_mdns_post_handler};

static const httpd_uri_t settings_pins_get_uri = {
    .uri = "/settings/pins",
    .method = HTTP_GET,
    .handler = settings_pins_get_handler};

static const httpd_uri_t settings_pins_post_uri = {
    .uri = "/settings/pins",
    .method = HTTP_POST,
    .handler = settings_pins_post_handler};

static const httpd_uri_t settings_networks_get_uri = {
    .uri = "/settings/networks",
    .method = HTTP_GET,
    .handler = settings_networks_get_handler};

static const httpd_uri_t settings_networks_post_uri = {
    .uri = "/settings/networks",
    .method = HTTP_POST,
    .handler = settings_networks_post_handler};

static const httpd_uri_t settings_networks_patch_uri = {
    .uri = "/settings/networks",
    .method = HTTP_PATCH,
    .handler = settings_networks_patch_handler};

static const httpd_uri_t settings_networks_delete_uri = {
    .uri = "/settings/networks",
    .method = HTTP_DELETE,
    .handler = settings_networks_delete_handler};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    conf.max_uri_handlers = 17;

    esp_err_t ret = httpd_start(&server, &conf);
    if (ESP_OK != ret)
    {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &reboot_uri);
    httpd_register_uri_handler(server, &target_flash_upload_post_uri);
    httpd_register_uri_handler(server, &target_flash_read_post_uri);
    httpd_register_uri_handler(server, &target_flash_erase_post_uri);
    httpd_register_uri_handler(server, &settings_mdns_get_uri);
    httpd_register_uri_handler(server, &settings_mdns_post_uri);
    httpd_register_uri_handler(server, &settings_pins_get_uri);
    httpd_register_uri_handler(server, &settings_pins_post_uri);
    httpd_register_uri_handler(server, &settings_networks_get_uri);
    httpd_register_uri_handler(server, &settings_networks_post_uri);
    httpd_register_uri_handler(server, &settings_networks_patch_uri);
    httpd_register_uri_handler(server, &settings_networks_delete_uri);
    return server;
}

void network_http_server_init(void)
{
    ESP_LOGI(TAG, "init http server");

    start_webserver();

    ESP_LOGI(TAG, "init rest server done");
}
