#include <string.h>
#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <hal/gpio_ll.h>
#include "driver/uart.h"

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include "network-serial.h"

#define SERIAL_PORT 2347
#define KEEPALIVE_IDLE 5
#define KEEPALIVE_INTERVAL 5
#define KEEPALIVE_COUNT 3
#define TAG "network-serial"

typedef struct
{
    bool connected;
    int socket_id;
} NetworkSerial;

static NetworkSerial network_serial;
static QueueHandle_t uart_event_queue;

bool network_serial_connected(void)
{
    return network_serial.connected;
}

void network_serial_send(uint8_t *buffer, size_t size)
{
    if (!network_serial.connected || network_serial.socket_id < 0)
        return;

    int to_write = size;
    while (to_write > 0)
    {
        int written = send(network_serial.socket_id, buffer + (size - to_write), to_write, 0);
        if (written <= 0)
        {
            ESP_LOGE(TAG, "Error sending data: errno %d", errno);
            network_serial.connected = false;
            break;
        }
        to_write -= written;
    }
}

static void uart_rx_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t buffer_rx[128];

    while (1)
    {
        if (xQueueReceive(uart_event_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        switch (event.type)
        {
        case UART_DATA:
        {
            size_t buffered_size = 0;
            ESP_ERROR_CHECK(uart_get_buffered_data_len(LP_UART_NUM_0, &buffered_size));

            while (buffered_size > 0)
            {
                int to_read = MIN((size_t)sizeof(buffer_rx), buffered_size);
                int read_size = uart_read_bytes(LP_UART_NUM_0, buffer_rx, to_read, portMAX_DELAY);
                if (read_size > 0)
                    network_serial_send(buffer_rx, read_size);

                buffered_size -= read_size > 0 ? read_size : 0;
            }
            break;
        }
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "UART overflow, flushing input");
            uart_flush_input(LP_UART_NUM_0);
            xQueueReset(uart_event_queue);
            break;
        default:
            break;
        }
    }
}

static void receive_and_send_to_serial(void)
{

    uint8_t buffer_rx[128];
    int rx_size = 0;

    do
    {
        rx_size = recv(network_serial.socket_id, buffer_rx, sizeof(buffer_rx), 0);
        if (rx_size > 0)
        {
            uart_write_bytes(LP_UART_NUM_0, (const char *)buffer_rx, rx_size);
        }
    } while (rx_size > 0);
}

static void network_serial_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    network_serial.connected = false;
    network_serial.socket_id = -1;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET)
    {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(SERIAL_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", SERIAL_PORT);

    err = listen(listen_sock, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1)
    {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

        // Convert ip address to string
        if (source_addr.ss_family == PF_INET)
        {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        network_serial.connected = true;
        network_serial.socket_id = sock;

        receive_and_send_to_serial();

        ESP_LOGI(TAG, "Socket closed");
        network_serial.connected = false;
        network_serial.socket_id = -1;
        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void network_serial_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = LP_UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(LP_UART_NUM_0, &uart_config));
    // LP UART on ESP32-C5 uses fixed pins: TX=GPIO5, RX=GPIO4
    ESP_ERROR_CHECK(uart_set_pin(LP_UART_NUM_0, 5, 4,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // LP UART FIFO is only 16 bytes, so use a TX buffer and an event queue for RX interrupts
    ESP_ERROR_CHECK(uart_driver_install(LP_UART_NUM_0, 256, 512, 16, &uart_event_queue, 0));

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);

    xTaskCreate(network_serial_task, "network_serial_task", 4096, (void *)AF_INET, 5, NULL);
}
