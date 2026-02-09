#include <network-http.h>
#include <esp_http_server.h>
#include "esp_event.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <sys/param.h>
#include <target.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "gdb-glue.h"
#include "network-http-page.h"

#define TAG "network-http"
#define FLASH_CHUNK_SIZE 4096      // Write in 4KB chunks for streaming
#define FLASH_BASE_ADDR 0x08000000 // Default ARM Cortex-M flash base

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/* Helper to find pattern in buffer */
static int find_pattern(const uint8_t *buffer, size_t buf_len, const char *pattern, size_t pattern_len)
{
    for (size_t i = 0; i <= buf_len - pattern_len; i++)
    {
        if (memcmp(buffer + i, pattern, pattern_len) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* File upload handler with streaming flash */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    uint8_t *chunk_buffer = NULL;
    uint8_t *header_buffer = NULL;
    target_s *target = NULL;
    size_t content_length = req->content_len;
    size_t total_received = 0;
    size_t total_written = 0;
    size_t data_start_offset = 0;
    int last_progress_reported = -1;
    bool success = false;
    bool headers_parsed = false;
    const char *error_msg = "Error: Flash operation failed";

    ESP_LOGI(TAG, "Starting streaming firmware flash, content size: %zu bytes", content_length);

    // Allocate buffers
    chunk_buffer = (uint8_t *)malloc(FLASH_CHUNK_SIZE);
    header_buffer = (uint8_t *)malloc(2048); // For parsing multipart headers
    if (!chunk_buffer || !header_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        const char *resp = "Error: Out of memory";
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, resp);
        if (chunk_buffer)
            free(chunk_buffer);
        if (header_buffer)
            free(header_buffer);
        return ESP_FAIL;
    }

    // Step 1: Parse multipart headers to find where binary data starts
    ESP_LOGI(TAG, "Parsing multipart headers...");
    size_t header_read = 0;
    const char *header_end_pattern = "\r\n\r\n";

    while (!headers_parsed && header_read < 2048 && header_read < content_length)
    {
        int recv_len = httpd_req_recv(req, (char *)(header_buffer + header_read),
                                      MIN(512, 2048 - header_read));
        if (recv_len <= 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            ESP_LOGE(TAG, "Failed to receive headers");
            error_msg = "Error: Failed to receive data";
            goto cleanup_early;
        }

        header_read += recv_len;
        total_received += recv_len;

        // Look for end of headers (\r\n\r\n)
        int end_pos = find_pattern(header_buffer, header_read, header_end_pattern, 4);
        if (end_pos >= 0)
        {
            data_start_offset = end_pos + 4; // Skip past \r\n\r\n
            headers_parsed = true;
            ESP_LOGI(TAG, "Headers end at offset %zu, binary data starts at %zu",
                     end_pos, data_start_offset);
        }
    }

    if (!headers_parsed)
    {
        ESP_LOGE(TAG, "Failed to find multipart headers boundary");
        error_msg = "Error: Invalid multipart format";
        goto cleanup_early;
    }

    // Calculate actual firmware size (exclude headers and trailing boundary)
    // Trailing boundary is typically ~50-100 bytes: \r\n------WebKitFormBoundary...\r\n
    size_t estimated_boundary_size = 100;
    size_t firmware_size = content_length - data_start_offset - estimated_boundary_size;
    ESP_LOGI(TAG, "Estimated firmware size: %zu bytes (content: %zu, headers: %zu)",
             firmware_size, content_length, data_start_offset);

    // Step 2: Scan for targets
    ESP_LOGI(TAG, "Scanning for targets...");

    char pkt_enable_noack[] = "$QStartNoAckMode#B0";
    gdb_glue_receive((uint8_t *)pkt_enable_noack, sizeof(pkt_enable_noack)); /* Enable NoAckMode */

    char cmd_reset[] = "$qRcmd,7265736574#37";
    gdb_glue_receive((uint8_t *)cmd_reset, sizeof(cmd_reset));
    extern target_s *target_list;
    int try = 5;
    while (target_list && try-- > 0)
    {
        ESP_LOGI(TAG, "Wait for target to halt...");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    bool target_found = false;
    if (adiv5_swd_scan())
    {
        ESP_LOGI(TAG, "Target found via SWD");
        target_found = true;
    }
    else if (jtag_scan())
    {
        ESP_LOGI(TAG, "Target found via JTAG");
        target_found = true;
    }

    if (!target_found)
    {
        ESP_LOGE(TAG, "No target found!");
        error_msg = "Error: No target device found";
        goto cleanup_early;
    }

    // Step 3: Attach to target
    target = target_attach_n(1, NULL);
    if (!target)
    {
        ESP_LOGE(TAG, "Failed to attach to target");
        gdb_glue_receive((uint8_t *)cmd_reset, sizeof(cmd_reset));
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        error_msg = "Error: Failed to attach to target";
        goto cleanup_early;
    }

    ESP_LOGI(TAG, "Successfully attached to target");

    // Step 4: Halt the target
    target_halt_request(target);
    vTaskDelay(pdMS_TO_TICKS(100));

    target_halt_reason_e halt_reason = target_halt_poll(target, NULL);
    if (halt_reason == TARGET_HALT_RUNNING || halt_reason == TARGET_HALT_ERROR)
    {
        ESP_LOGE(TAG, "Failed to halt target");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Target halted");

    // Step 5: Erase flash
    ESP_LOGI(TAG, "Erasing flash at 0x%08lX, size: %zu bytes", FLASH_BASE_ADDR, firmware_size);
    if (!target_flash_erase(target, FLASH_BASE_ADDR, firmware_size))
    {
        ESP_LOGE(TAG, "Flash erase failed");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Flash erased, starting streaming write...");

    // Step 6: Stream data and write to flash
    // First, handle any binary data already in header_buffer
    size_t chunk_offset = 0;
    size_t binary_in_header = header_read - data_start_offset;
    if (binary_in_header > 0)
    {
        memcpy(chunk_buffer, header_buffer + data_start_offset, binary_in_header);
        chunk_offset = binary_in_header;
    }
    free(header_buffer);
    header_buffer = NULL;

    size_t remaining = content_length - total_received;
    size_t target_bytes_to_write = firmware_size; // How many bytes to actually write to target

    while (remaining > 0 && total_written < target_bytes_to_write)
    {
        // Read data chunk
        size_t to_read = MIN(FLASH_CHUNK_SIZE - chunk_offset, remaining);
        int recv_len = httpd_req_recv(req, (char *)(chunk_buffer + chunk_offset), to_read);

        if (recv_len <= 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            ESP_LOGE(TAG, "Failed to receive data at offset %zu", total_received);
            goto cleanup;
        }

        total_received += recv_len;
        chunk_offset += recv_len;
        remaining -= recv_len;

        // Write chunk to target flash when buffer is full or we've reached target size
        size_t bytes_to_flash = chunk_offset;
        if (total_written + bytes_to_flash > target_bytes_to_write)
        {
            bytes_to_flash = target_bytes_to_write - total_written;
        }

        if (bytes_to_flash >= FLASH_CHUNK_SIZE ||
            (total_written + bytes_to_flash >= target_bytes_to_write))
        {

            if (!target_flash_write(target, FLASH_BASE_ADDR + total_written,
                                    chunk_buffer, bytes_to_flash))
            {
                ESP_LOGE(TAG, "Flash write failed at offset %zu", total_written);
                goto cleanup;
            }

            total_written += bytes_to_flash;

            // Move remaining data to start of buffer
            if (chunk_offset > bytes_to_flash)
            {
                memmove(chunk_buffer, chunk_buffer + bytes_to_flash,
                        chunk_offset - bytes_to_flash);
                chunk_offset -= bytes_to_flash;
            }
            else
            {
                chunk_offset = 0;
            }

            // Log progress every 10%
            int progress = (int)(total_written * 100 / target_bytes_to_write);
            if (progress / 10 > last_progress_reported / 10)
            {
                ESP_LOGI(TAG, "Flash progress: %zu / %zu bytes (%d%%)",
                         total_written, target_bytes_to_write, progress);
                last_progress_reported = progress;
            }
        }
    }

    // Drain any remaining HTTP data
    while (remaining > 0)
    {
        char discard[256];
        int recv_len = httpd_req_recv(req, discard, MIN(sizeof(discard), remaining));
        if (recv_len <= 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            break;
        }
        remaining -= recv_len;
    }

    ESP_LOGI(TAG, "Streaming write complete: %zu bytes written to target", total_written);

    // Step 6: Complete flash operation
    if (!target_flash_complete(target))
    {
        ESP_LOGW(TAG, "Flash complete operation returned false");
    }

    // Step 7: Reset target
    ESP_LOGI(TAG, "Resetting target...");
    target_reset(target);
    target_halt_resume(target, false);

    success = true;
    ESP_LOGI(TAG, "Flash operation completed successfully!");

cleanup:
    if (target)
    {
        target_detach(target);
    }
    gdb_glue_receive((uint8_t *)cmd_reset, sizeof(cmd_reset));
    vTaskDelay(3000 / portTICK_PERIOD_MS);
cleanup_early:
    char pkt_disable_noack[] = "\x04";
    gdb_glue_receive((uint8_t *)pkt_disable_noack, 1);
    if (header_buffer)
        free(header_buffer);
    if (chunk_buffer)
        free(chunk_buffer);

    if (success)
    {
        const char *resp = "Firmware flashed successfully";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        return ESP_FAIL;
    }
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler};

static const httpd_uri_t upload = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();

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
