#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/usb_serial_jtag.h>

#include "uart-gdb.h"
#include <gdb-glue.h>

#define TAG "uart-gdb"

#define USB_GDB_RX_BUF_SIZE 4096
#define USB_GDB_TX_BUF_SIZE 4096
#define USB_GDB_IO_TIMEOUT_MS 100

typedef struct
{
    bool connected;
} UartGDB;

static UartGDB uart_gdb;

static void delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

bool uart_gdb_connected(void)
{
    return uart_gdb.connected;
}

void uart_gdb_send(uint8_t* buffer, size_t size)
{
    int to_write = size;
    while (to_write > 0)
    {
        int written = usb_serial_jtag_write_bytes(buffer + (size - to_write), to_write,
                                                  pdMS_TO_TICKS(USB_GDB_IO_TIMEOUT_MS));
        if (written < 0)
            return;
        to_write -= written;
    }
}

static void receive_and_send_to_gdb(void)
{
    size_t   gdb_packet_size = gdb_glue_get_packet_size();
    uint8_t* buffer_rx       = malloc(gdb_packet_size);

    while (1)
    {
        if (gdb_glue_can_receive())
        {
            size_t max_len = gdb_glue_get_free_size();
            if (max_len > gdb_packet_size)
                max_len = gdb_packet_size;
            if (max_len == 0)
            {
                delay(10);
                continue;
            }

            int rx_size = usb_serial_jtag_read_bytes(buffer_rx, max_len, pdMS_TO_TICKS(USB_GDB_IO_TIMEOUT_MS));
            if (rx_size > 0)
            {
                gdb_glue_receive(buffer_rx, rx_size);
            }
        }
        else
        {
            delay(10);
        }
    }
}

static void uart_gdb_server_task(void* pvParameters)
{
    uart_gdb.connected = false;

    // The console/logs live on UART0, so the USB-Serial-JTAG peripheral is free
    // for the GDB transport. The USB peripheral has no pins and no baud rate:
    // only the driver ring buffer sizes need to be configured.
    usb_serial_jtag_driver_config_t usb_config = {
        .rx_buffer_size = USB_GDB_RX_BUF_SIZE,
        .tx_buffer_size = USB_GDB_TX_BUF_SIZE,
    };

    // Install the driver to get buffered access to RX/TX.
    esp_err_t err = usb_serial_jtag_driver_install(&usb_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "GDB over USB-Serial-JTAG started");

    uart_gdb.connected = true;

    receive_and_send_to_gdb();

    // Unreachable in normal operation
    uart_gdb.connected = false;
    usb_serial_jtag_driver_uninstall();
    vTaskDelete(NULL);
}

void uart_gdb_server_init(void)
{
    uart_gdb.connected = false;

    xTaskCreate(uart_gdb_server_task, "uart_gdb_server", 4096, NULL, 5, NULL);
}
