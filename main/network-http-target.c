#include <esp_log.h>
#include <esp_http_server.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "just_flasher.h"
#include "general.h"
#include "network-bmcloud.h"

#define TAG "network-http-target"

#define FLASH_CHUNK_SIZE        4096  // Write in 4KB chunks for streaming
#define FLASHER_LOCK_WAIT_MS    1000  // fail fast when another flash operation is running

/* Helper to find pattern in buffer */
static int find_pattern(const uint8_t* buffer, size_t buf_len, const char* pattern, size_t pattern_len)
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

/* "Error: <flasher message>" for the HTTP response */
static const char* flasher_web_error(JustFlasherError err)
{
    static char buffer[64];
    snprintf(buffer, sizeof(buffer), "Error: %s", just_flasher_err_str(err));
    return buffer;
}

typedef struct
{
    uint32_t base_addr;
    uint32_t length;
    bool     use_swd;
} flash_params_t;

static flash_params_t flash_params(httpd_req_t* req)
{
    flash_params_t params = {.base_addr = FLASHER_DEFAULT_BASE_ADDR, .length = 0, .use_swd = true};

    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char val[32];
        if (httpd_query_key_value(query, "baseAddr", val, sizeof(val)) == ESP_OK)
            params.base_addr = strtoul(val, NULL, 16);
        if (httpd_query_key_value(query, "iface", val, sizeof(val)) == ESP_OK)
            params.use_swd = (strncmp(val, "swd", 3) == 0);
        if (httpd_query_key_value(query, "length", val, sizeof(val)) == ESP_OK)
            params.length = strtoul(val, NULL, 10);
    }
    return params;
}

/* File upload handler with streaming flash */
esp_err_t target_flash_upload_post_handler(httpd_req_t* req)
{
    uint8_t*    chunk_buffer           = NULL;
    uint8_t*    header_buffer          = NULL;
    size_t      content_length         = req->content_len;
    size_t      total_received         = 0;
    size_t      total_written          = 0;
    size_t      data_start_offset      = 0;
    int         last_progress_reported = -1;
    bool        success                = false;
    bool        headers_parsed         = false;
    const char* error_msg              = "Error: Flash operation failed";

    flash_params_t params = flash_params(req);

    ESP_LOGI(TAG, "Starting streaming firmware flash, content size: %zu bytes", content_length);

    // Allocate buffers
    chunk_buffer  = (uint8_t*) malloc(FLASH_CHUNK_SIZE);
    header_buffer = (uint8_t*) malloc(2048);  // For parsing multipart headers
    if (!chunk_buffer || !header_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        const char* resp = "Error: Out of memory";
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, resp);
        if (chunk_buffer)
            free(chunk_buffer);
        if (header_buffer)
            free(header_buffer);
        network_bmcloud_set_status(CloudDeviceStateError);
        return ESP_FAIL;
    }
    network_bmcloud_set_status(CloudDeviceStateBusy);

    // Step 1: Parse multipart headers to find where binary data starts
    ESP_LOGI(TAG, "Parsing multipart headers...");
    size_t      header_read        = 0;
    const char* header_end_pattern = "\r\n\r\n";

    while (!headers_parsed && header_read < 2048 && header_read < content_length)
    {
        int recv_len = httpd_req_recv(req, (char*) (header_buffer + header_read), MIN(512, 2048 - header_read));
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
            data_start_offset = end_pos + 4;  // Skip past \r\n\r\n
            headers_parsed    = true;
            ESP_LOGI(TAG, "Headers end at offset %zu, binary data starts at %zu", end_pos, data_start_offset);
        }
    }

    if (!headers_parsed)
    {
        ESP_LOGE(TAG, "Failed to find multipart headers boundary");
        error_msg = "Error: Invalid multipart format";
        goto cleanup_early;
    }

    ESP_LOGI(TAG, "Using flash base address: 0x%08lX", (unsigned long) params.base_addr);

    size_t firmware_size = params.length;
    if (firmware_size == 0)
    {
        ESP_LOGE(TAG, "Recieved 0 size of firmware");
        error_msg = "Error: Recieved 0 size of firmware";
        goto cleanup_early;
    }

    ESP_LOGI(TAG,
             "Estimated firmware size: %zu bytes (content: %zu, headers: %zu)",
             firmware_size,
             content_length,
             data_start_offset);

    // Step 2: Take exclusive ownership of the target and prepare it
    JustFlasherError err = just_flasher_begin(params.use_swd, FLASHER_LOCK_WAIT_MS);
    if (err != JustFlasherOk)
    {
        ESP_LOGE(TAG, "Failed to start flash operation: %s", just_flasher_err_str(err));
        error_msg = flasher_web_error(err);
        goto cleanup_early;
    }

    // Step 3: Erase flash
    err = just_flasher_erase(params.base_addr, firmware_size);
    if (err != JustFlasherOk)
    {
        error_msg = flasher_web_error(err);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Flash erased, starting streaming write...");

    // Step 4: Stream data and write to flash
    // First, handle any binary data already in header_buffer
    size_t chunk_offset     = 0;
    size_t binary_in_header = header_read - data_start_offset;
    if (binary_in_header > 0)
    {
        memcpy(chunk_buffer, header_buffer + data_start_offset, binary_in_header);
        chunk_offset = binary_in_header;
    }
    free(header_buffer);
    header_buffer = NULL;

    size_t remaining             = content_length - total_received;
    size_t target_bytes_to_write = firmware_size;  // How many bytes to actually write to target

    while (remaining > 0 && total_written < target_bytes_to_write)
    {
        // Read data chunk
        size_t to_read  = MIN(FLASH_CHUNK_SIZE - chunk_offset, remaining);
        int    recv_len = httpd_req_recv(req, (char*) (chunk_buffer + chunk_offset), to_read);

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

        if (bytes_to_flash >= FLASH_CHUNK_SIZE || (total_written + bytes_to_flash >= target_bytes_to_write))
        {
            if (just_flasher_write(params.base_addr + total_written, chunk_buffer, bytes_to_flash) != JustFlasherOk)
            {
                ESP_LOGE(TAG, "Flash write failed at offset %zu", total_written);
                error_msg = flasher_web_error(JustFlasherErrWrite);
                goto cleanup;
            }

            total_written += bytes_to_flash;

            // Move remaining data to start of buffer
            if (chunk_offset > bytes_to_flash)
            {
                memmove(chunk_buffer, chunk_buffer + bytes_to_flash, chunk_offset - bytes_to_flash);
                chunk_offset -= bytes_to_flash;
            }
            else
            {
                chunk_offset = 0;
            }

            // Log progress every 10%
            int progress = (int) (total_written * 100 / target_bytes_to_write);
            if (progress / 10 > last_progress_reported / 10)
            {
                ESP_LOGI(TAG, "Flash progress: %zu / %zu bytes (%d%%)", total_written, target_bytes_to_write, progress);
                last_progress_reported = progress;
            }
        }
    }

    // Drain any remaining HTTP data
    while (remaining > 0)
    {
        char discard[256];
        int  recv_len = httpd_req_recv(req, discard, MIN(sizeof(discard), remaining));
        if (recv_len <= 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            break;
        }
        remaining -= recv_len;
    }

    ESP_LOGI(TAG, "Streaming write complete: %zu bytes written to target", total_written);

    // Step 5: Flush writes, reset the target and release it
    just_flasher_end();

    success = true;
    ESP_LOGI(TAG, "Flash operation completed successfully!");

cleanup:
    just_flasher_abort();
cleanup_early:
    if (header_buffer)
        free(header_buffer);
    if (chunk_buffer)
        free(chunk_buffer);

    if (success)
    {
        const char* resp = "Firmware flashed successfully";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        network_bmcloud_set_status(CloudDeviceStateOnline);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        network_bmcloud_set_status(CloudDeviceStateError);
        return ESP_FAIL;
    }
}

/* Flash erase handler */
esp_err_t target_flash_erase_post_handler(httpd_req_t* req)
{
    bool        success   = false;
    const char* error_msg = "Error: Flash operation failed";

    flash_params_t params = flash_params(req);

    network_bmcloud_set_status(CloudDeviceStateBusy);

    // Step 1: Take exclusive ownership of the target and prepare it
    JustFlasherError err = just_flasher_begin(params.use_swd, FLASHER_LOCK_WAIT_MS);
    if (err != JustFlasherOk)
    {
        ESP_LOGE(TAG, "Failed to start erase operation: %s", just_flasher_err_str(err));
        error_msg = flasher_web_error(err);
        goto cleanup_early;
    }

    // Step 2: Erase flash, length 0 erases the whole main flash bank
    err = just_flasher_erase(params.base_addr, params.length);
    if (err != JustFlasherOk)
    {
        error_msg = flasher_web_error(err);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Flash erased successfully");

    // Step 3: Reset and resume the target, release it
    just_flasher_end();

    success = true;

cleanup:
    just_flasher_abort();
cleanup_early:
    if (success)
    {
        const char* resp = "Flash erased successfully";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        network_bmcloud_set_status(CloudDeviceStateOnline);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        network_bmcloud_set_status(CloudDeviceStateError);
        return ESP_FAIL;
    }
}

/* Flash read handler */
esp_err_t target_flash_read_post_handler(httpd_req_t* req)
{
    bool        success        = false;
    const char* error_msg      = "Error: Flash operation failed";
    uint8_t*    chunk_buffer   = malloc(FLASH_CHUNK_SIZE);
    bool        stream_started = false;

    flash_params_t params = flash_params(req);

    if (!chunk_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error: Out of memory");
        return ESP_FAIL;
    }

    network_bmcloud_set_status(CloudDeviceStateBusy);

    // Step 1: Take exclusive ownership of the target and prepare it
    JustFlasherError err = just_flasher_begin(params.use_swd, FLASHER_LOCK_WAIT_MS);
    if (err != JustFlasherOk)
    {
        ESP_LOGE(TAG, "Failed to start read operation: %s", just_flasher_err_str(err));
        error_msg = flasher_web_error(err);
        goto cleanup_early;
    }

    // Step 2: Read length: explicit or the whole main flash bank
    size_t read_len = params.length;
    {
        read_len = just_flasher_get_flash_size();
    }
    ESP_LOGI(TAG, "Reading flash at 0x%08lX, size: %zu bytes", (unsigned long) params.base_addr, read_len);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"flash_dump.bin\"");
    char read_len_str[12];
    itoa(read_len, read_len_str, 10);
    httpd_resp_set_hdr(req, "Content-Length", read_len_str);
    stream_started = true;

    // Step 3: Read flash and stream it to the client
    for (size_t offset = 0; offset < read_len;)
    {
        size_t chunk = MIN(read_len - offset, FLASH_CHUNK_SIZE);
        if (just_flasher_read(params.base_addr + offset, chunk_buffer, chunk) != JustFlasherOk)
        {
            ESP_LOGE(TAG, "Flash read failed at 0x%08lX", (unsigned long) (params.base_addr + offset));
            error_msg = flasher_web_error(JustFlasherErrRead);
            httpd_resp_send_chunk(req, NULL, 0);
            goto cleanup;
        }

        if (httpd_resp_send_chunk(req, (const char*) chunk_buffer, chunk) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send chunk to client");
            error_msg = "Error: Failed to send data";
            httpd_resp_send_chunk(req, NULL, 0);
            goto cleanup;
        }

        offset += chunk;
    }
    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI(TAG, "Flash read successfully");

    // Step 4: Reset and resume the target, release it
    just_flasher_end();

    success = true;

cleanup:
    just_flasher_abort();
    free(chunk_buffer);
cleanup_early:
    if (success)
    {
        network_bmcloud_set_status(CloudDeviceStateOnline);
        return ESP_OK;
    }
    else if (!stream_started)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        network_bmcloud_set_status(CloudDeviceStateError);
        return ESP_FAIL;
    }
    else
    {
        // Response body already started: the client detects the truncated dump by size
        network_bmcloud_set_status(CloudDeviceStateOnline);
        return ESP_OK;
    }
}
