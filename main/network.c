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
#include "nvs-config.h"

#define TAG "network"

#define DEFAULT_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define DEFAULT_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define DEFAULT_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define DEFAULT_GTK_REKEY_INTERVAL 0
#endif

#define MDNS_INSTANCE "blackmagic web server"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static volatile bool s_wifi_scanning = true;

void network_hostnames_init(void)
{
    mstring_t *hostname = mstring_alloc();
    nvs_config_get_hostname(hostname);

    ESP_LOGI(TAG, "init mdns");

    mdns_init();

    mdns_hostname_set(mstring_get_cstr(hostname));
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
    netbiosns_set_name(mstring_get_cstr(hostname));
    ESP_LOGI(TAG, "init netbios done");

    mstring_free(hostname);
}

static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base,
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
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_wifi_scanning)
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        else
        {
            ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
            esp_wifi_connect();
        }
    }
}

static void wifi_sta_begin(void)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool wifi_sta_try(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    ESP_LOGI(TAG, "Waiting for connection to '%s'...", ssid);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        return true;
    }
    esp_wifi_disconnect();
    return false;
}

static void wifi_switch_to_ap(const char *ssid, const char *pass)
{
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = DEFAULT_WIFI_CHANNEL,
            .max_connection = DEFAULT_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else
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

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, pass, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);

    if (strlen(pass) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP mode started: SSID=%s", ssid);
}

void network_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    s_wifi_scanning = true;

    ESP_LOGI(TAG, "Initializing network");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    network_hostnames_init();
    wifi_sta_begin();

    bool connected = false;
    int32_t net_count = 0;
    nvs_config_get_networks_count(&net_count);

    ESP_LOGI(TAG, "Trying %ld known network(s)...", (long)net_count);
    for (int32_t i = 0; i < net_count && !connected; i++)
    {
        wifi_network_t net;
        nvs_config_get_network(i, &net);

        if (!net.auto_connect || net.ssid[0] == '\0') continue;

        ESP_LOGI(TAG, "Trying network [%ld]: %s", (long)i, net.ssid);
        connected = wifi_sta_try(net.ssid, net.pass);
        if (!connected)
        {
            ESP_LOGW(TAG, "Failed to connect to %s", net.ssid);
        }
    }

    s_wifi_scanning = false;

    if (!connected)
    {
        ESP_LOGW(TAG, "All STA connections failed, switching to AP mode");
        wifi_switch_to_ap("blackmagic", "blackmagic");
    }
    else
    {
        ESP_LOGI(TAG, "Connected to WiFi as STA");
    }
}