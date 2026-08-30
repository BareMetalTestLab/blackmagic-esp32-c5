#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/uart.h>

#include "uart-rtt.h"

#ifdef ENABLE_RTT
#    include "rtt_if_esp32.h"
#endif

#define TAG "uart-rtt"

#define UART_RTT_BAUD_RATE 115200
#define UART_RTT_TX_PIN 10
#define UART_RTT_RX_PIN 9
#define UART_RTT_RX_BUF_SIZE 4096
#define UART_RTT_TX_BUF_SIZE 4096
#define UART_RTT_IO_TIMEOUT_MS 100

typedef struct
{
    bool connected;
} UartRTT;

static UartRTT uart_rtt;

bool uart_rtt_connected(void)
{
    return uart_rtt.connected;
}

void uart_rtt_send(uint8_t* buffer, size_t size)
{
    if (!uart_rtt.connected)
        return;

    int to_write = size;
    ESP_LOGI(TAG, "send %s", buffer);
    while (to_write > 0)
    {
        int written = uart_write_bytes(UART_NUM_1, buffer + (size - to_write), to_write);
        if (written < 0)
        {
            ESP_LOGE(TAG, "Error sending data");
            uart_rtt.connected = false;
            break;
        }
        to_write -= written;
    }
}

#ifdef ENABLE_RTT
static void receive_and_send_to_rtt(void)
{
    uint8_t buffer_rx[128];
    int     rx_size = 0;

    do
    {
        rx_size = uart_read_bytes(UART_NUM_1, buffer_rx, sizeof(buffer_rx), pdMS_TO_TICKS(UART_RTT_IO_TIMEOUT_MS));
        if (rx_size > 0)
        {
            ESP_LOGI(TAG, "resieved %s", buffer_rx);
            rtt_receive_data(buffer_rx, rx_size);
        }
    } while (rx_size > 0);
}
#endif

static void uart_rtt_server_task(void* pvParameters)
{
    uart_rtt.connected = false;

    // The console/logs live on UART0 and GDB on the USB-Serial-JTAG peripheral,
    // so UART1 is free for the RTT transport via an external USB-to-TTL adapter.
    uart_config_t uart_config = {
        .baud_rate  = UART_RTT_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install the driver to get buffered access to RX/TX.
    esp_err_t err = uart_driver_install(UART_NUM_1, UART_RTT_RX_BUF_SIZE, UART_RTT_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = uart_param_config(UART_NUM_1, &uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(UART_NUM_1);
        vTaskDelete(NULL);
        return;
    }

    err = uart_set_pin(UART_NUM_1, UART_RTT_TX_PIN, UART_RTT_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(UART_NUM_1);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG,
             "RTT over UART1 started (TX=%d, RX=%d, %d baud)",
             UART_RTT_TX_PIN,
             UART_RTT_RX_PIN,
             UART_RTT_BAUD_RATE);

    // Unlike TCP, a UART has no connection events: once the driver is installed
    // the link is considered "connected" and is served forever.
    uart_rtt.connected = true;

#ifdef ENABLE_RTT
    while (1)
    {
        receive_and_send_to_rtt();
    }
#else
    // Nothing to do without RTT support: drain RX so the buffer does not fill up.
    uint8_t dummy;
    while (uart_read_bytes(UART_NUM_1, &dummy, 1, portMAX_DELAY) > 0)
    {
    }
#endif

    // Unreachable in normal operation
    uart_rtt.connected = false;
    uart_driver_delete(UART_NUM_1);
    vTaskDelete(NULL);
}

void uart_rtt_server_init(void)
{
#ifdef ENABLE_RTT
    rtt_if_init();
#endif

    xTaskCreate(uart_rtt_server_task, "uart_rtt_server", 4096, NULL, 5, NULL);
}
