#include <string.h>
#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include "network-rtt.h"

#ifdef ENABLE_RTT
#include "rtt_if_esp32.h"
#endif

#define RTT_PORT 2346
#define KEEPALIVE_IDLE 5
#define KEEPALIVE_INTERVAL 5
#define KEEPALIVE_COUNT 3
#define TAG "network-rtt"

typedef struct
{
    bool connected;
    int socket_id;
} NetworkRTT;

static NetworkRTT network_rtt;

bool network_rtt_connected(void)
{
    return network_rtt.connected;
}

void network_rtt_send(uint8_t *buffer, size_t size)
{
    if (!network_rtt.connected || network_rtt.socket_id < 0)
        return;
        
    int to_write = size;
    while (to_write > 0)
    {
        int written = send(network_rtt.socket_id, buffer + (size - to_write), to_write, 0);
        if (written < 0) {
            ESP_LOGE(TAG, "Error sending data: errno %d", errno);
            network_rtt.connected = false;
            break;
        }
        to_write -= written;
    }
}

#ifdef ENABLE_RTT
static void receive_and_send_to_rtt(void)
{
    uint8_t buffer_rx[128];
    int rx_size = 0;

    do
    {
        rx_size = recv(network_rtt.socket_id, buffer_rx, sizeof(buffer_rx), 0);
        if (rx_size > 0)
        {
            // Send received data to target via RTT
            rtt_receive_data(buffer_rx, rx_size);
        }
    } while (rx_size > 0);
}
#endif

static void network_rtt_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    network_rtt.connected = false;
    network_rtt.socket_id = -1;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET)
    {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(RTT_PORT);
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
    ESP_LOGI(TAG, "Socket bound, port %d", RTT_PORT);

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

        network_rtt.connected = true;
        network_rtt.socket_id = sock;

#ifdef ENABLE_RTT
        receive_and_send_to_rtt();
#else
        // Just wait for disconnect if RTT is not enabled
        char dummy;
        while (recv(sock, &dummy, 1, 0) > 0);
#endif

        ESP_LOGI(TAG, "Socket closed");
        network_rtt.connected = false;
        network_rtt.socket_id = -1;
        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void network_rtt_server_init(void)
{
    rtt_if_init();
    xTaskCreate(network_rtt_server_task, "rtt_server", 4096, (void *)AF_INET, 5, NULL);
}
