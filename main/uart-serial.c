#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "uart-serial.h"

#ifdef ENABLE_UART_SERIAL

#define TAG "uart-serial"

typedef struct
{
    bool connected;
} UartSerial;

static UartSerial uart_serial;

// LP UART driver events (RX interrupts)
static QueueHandle_t uart_event_queue;

bool uart_serial_connected(void)
{
    return uart_serial.connected;
}

void uart_serial_send(uint8_t *buffer, size_t size)
{
    if (!uart_serial.connected)
        return;

    int to_write = size;
    while (to_write > 0)
    {
        int written = uart_write_bytes(UART_NUM_0, (const char *)buffer + (size - to_write), to_write);
        if (written <= 0)
        {
            ESP_LOGE(TAG, "Error sending data to UART0");
            uart_serial.connected = false;
            break;
        }
        to_write -= written;
    }
}

// LP UART -> UART0: pump the LP UART event queue and forward the data to UART0
static void uart_rx_task(void *pvParameters)
{
    uart_event_t event;
    while (1)
    {
        // Waiting for UART event
        if (xQueueReceive(uart_event_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            switch (event.type)
            {
            // Event of UART receiving data
            case UART_DATA:
            {
                size_t buffered_size;
                uart_get_buffered_data_len(LP_UART_NUM_0, &buffered_size);
                while (buffered_size > 0)
                {
                    uint8_t buffer_rx[128];
                    int to_read = MIN((size_t)sizeof(buffer_rx), buffered_size);
                    int read_size = uart_read_bytes(LP_UART_NUM_0, buffer_rx, to_read, portMAX_DELAY);
                    if (read_size > 0)
                    {
                        uart_serial_send(buffer_rx, read_size);
                        buffered_size -= read_size;
                    }
                    else
                    {
                        break;
                    }
                }
                break;
            }
            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
            {
                ESP_LOGW(TAG, "UART overflow, flushing input");
                uart_flush_input(LP_UART_NUM_0);
                xQueueReset(uart_event_queue);
                break;
            }
            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

// UART0 -> LP UART: read from UART0 and forward the data to the LP UART
static void uart0_rx_task(void *pvParameters)
{
    uint8_t buffer_rx[128];
    while (1)
    {
        int rx_size = uart_read_bytes(UART_NUM_0, buffer_rx, sizeof(buffer_rx), portMAX_DELAY);
        if (rx_size > 0)
        {
            uart_write_bytes(LP_UART_NUM_0, (const char *)buffer_rx, rx_size);
        }
    }
    vTaskDelete(NULL);
}

void uart_serial_init(void)
{
    // LP UART: 115200 8N1, fixed pins TX=GPIO5, RX=GPIO4
    const uart_config_t lp_uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = LP_UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(LP_UART_NUM_0, &lp_uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LP UART uart_param_config failed: %s", esp_err_to_name(err));
        return;
    }
    // LP UART on ESP32-C5 uses fixed pins: TX=GPIO5, RX=GPIO4
    err = uart_set_pin(LP_UART_NUM_0, 5, 4, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LP UART uart_set_pin failed: %s", esp_err_to_name(err));
        return;
    }
    // LP UART FIFO is only 16 bytes, so use a TX buffer and an event queue for RX interrupts
    err = uart_driver_install(LP_UART_NUM_0, 256, 512, 16, &uart_event_queue, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LP UART uart_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    // UART0 (console port): 115200 8N1, keep the default console pins
    const uart_config_t uart0_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_param_config(UART_NUM_0, &uart0_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART0 uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(LP_UART_NUM_0);
        return;
    }
    err = uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART0 uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(LP_UART_NUM_0);
        return;
    }
    err = uart_driver_install(UART_NUM_0, 256, 512, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART0 uart_driver_install failed: %s", esp_err_to_name(err));
        uart_driver_delete(LP_UART_NUM_0);
        return;
    }

    // Unlike TCP, a UART has no connection events: once both drivers are
    // installed the bridge is considered "connected".
    uart_serial.connected = true;

    // LP UART -> UART0
    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);
    // UART0 -> LP UART
    xTaskCreate(uart0_rx_task, "uart0_rx_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Serial bridge UART0 <-> LP UART started (TX=5, RX=4, 115200 baud)");
}

#endif // ENABLE_UART_SERIAL
