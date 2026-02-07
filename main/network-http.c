#include <network-http.h>
#include <esp_http_server.h>
#include "esp_event.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <sys/param.h>

#define TAG "network-http"

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html_page = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>BlackMagic - Upload Firmware</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; }"
        "h1 { color: #333; }"
        ".upload-form { border: 2px dashed #ccc; padding: 30px; border-radius: 10px; text-align: center; }"
        "input[type='file'] { margin: 20px 0; }"
        "button { background-color: #4CAF50; color: white; padding: 12px 30px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }"
        "button:hover { background-color: #45a049; }"
        "#status { margin-top: 20px; padding: 10px; border-radius: 5px; }"
        ".success { background-color: #d4edda; color: #155724; }"
        ".error { background-color: #f8d7da; color: #721c24; }"
        "</style>"
        "</head>"
        "<body>"
        "<h1>BlackMagic Probe - Firmware Upload</h1>"
        "<div class='upload-form'>"
        "<h2>Select Firmware File</h2>"
        "<form id='uploadForm' enctype='multipart/form-data'>"
        "<input type='file' id='fileInput' name='file' accept='.bin,.elf' required>"
        "<br>"
        "<button type='submit'>Upload & Flash</button>"
        "</form>"
        "<div id='status'></div>"
        "</div>"
        "<script>"
        "document.getElementById('uploadForm').addEventListener('submit', async (e) => {"
        "  e.preventDefault();"
        "  const fileInput = document.getElementById('fileInput');"
        "  const file = fileInput.files[0];"
        "  if (!file) return;"
        "  const status = document.getElementById('status');"
        "  status.textContent = 'Uploading ' + file.name + '...';"
        "  status.className = '';"
        "  const formData = new FormData();"
        "  formData.append('file', file);"
        "  try {"
        "    const response = await fetch('/upload', { method: 'POST', body: formData });"
        "    const result = await response.text();"
        "    if (response.ok) {"
        "      status.textContent = 'Success: ' + result;"
        "      status.className = 'success';"
        "    } else {"
        "      status.textContent = 'Error: ' + result;"
        "      status.className = 'error';"
        "    }"
        "  } catch (error) {"
        "    status.textContent = 'Upload failed: ' + error.message;"
        "    status.className = 'error';"
        "  }"
        "});"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/* File upload handler */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char buf[512];
    int ret, remaining = req->content_len;
    
    ESP_LOGI(TAG, "Receiving file upload, size: %d bytes", remaining);
    
    // Read and process the uploaded file
    size_t total_received = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        total_received += recv_len;
        remaining -= recv_len;
        
        // TODO: Here you can write the data to flash or process it
        // For now, just logging progress
        ESP_LOGI(TAG, "Received %zu / %d bytes", total_received, req->content_len);
    }
    
    ESP_LOGI(TAG, "File upload complete: %zu bytes", total_received);
    
    const char* resp = "File uploaded successfully";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};

static const httpd_uri_t upload = {
    .uri       = "/upload",
    .method    = HTTP_POST,
    .handler   = upload_post_handler
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();

    esp_err_t ret = httpd_start(&server, &conf);
    if (ESP_OK != ret) {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &upload);
    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

void network_http_server_init(void)
{
    ESP_LOGI(TAG, "init http server");

    start_webserver();

    ESP_LOGI(TAG, "init rest server done");
}
