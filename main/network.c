#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "network.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <lwip/apps/netbiosns.h>
#include <mdns.h>

#define TAG "network"

#define DEFAULT_WIFI_SSID "blackmagic"
#define DEFAULT_WIFI_PASS "blackmagic"
#define DEFAULT_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define DEFAULT_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define DEFAULT_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define DEFAULT_GTK_REKEY_INTERVAL 0
#endif

#define MDNS_HOST_NAME "blackmagic"
#define MDNS_INSTANCE "blackmagic web server"

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_event_group;

void network_hostnames_init(void)
{

    ESP_LOGI(TAG, "init mdns");
    char *hostname = MDNS_HOST_NAME;
    mdns_init();

    mdns_hostname_set(hostname);
    mdns_instance_name_set(MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {{"board", "esp32"}, {"path", "/"}};

    ESP_ERROR_CHECK(mdns_service_add(
        "ESP32-WebServer",
        "_http",
        "_tcp",
        80,
        serviceTxtData,
        sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));

    ESP_LOGI(TAG, "init MDNS done");

    ESP_LOGI(TAG, "init netbios");
    netbiosns_init();
    netbiosns_set_name(hostname);
    ESP_LOGI(TAG, "init netbios done");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    }
}

static void wifi_init_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_WIFI_SSID,
            .password = DEFAULT_WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished. SSID:%s password:%s", DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
}

static void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = DEFAULT_WIFI_SSID,
            .ssid_len = strlen(DEFAULT_WIFI_SSID),
            .channel = DEFAULT_WIFI_CHANNEL,
            .password = DEFAULT_WIFI_PASS,
            .max_connection = DEFAULT_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                .required = true,
            },
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
#endif
            .gtk_rekey_interval = DEFAULT_GTK_REKEY_INTERVAL,
        },
    };
    if (strlen(DEFAULT_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, DEFAULT_WIFI_CHANNEL);
}

void network_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    network_hostnames_init();
    wifi_init_sta();

    ESP_LOGI(TAG, "Connecting to WiFi as STA...");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(5000)
    );

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "STA connect failed, switching to AP mode");
        wifi_init_softap();
    } else {
        ESP_LOGI(TAG, "Connected to AP as STA");
    }
}