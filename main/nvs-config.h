#pragma once

#include <m-string.h>
#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

esp_err_t nvs_config_set_hostname(const mstring_t* hostname);
esp_err_t nvs_config_get_hostname(mstring_t* hostname);

esp_err_t nvs_config_set_pins(int32_t swdio, int32_t swclk, int32_t tdi, int32_t tdo, int32_t trst);
esp_err_t nvs_config_get_pins(int32_t *swdio, int32_t *swclk, int32_t *tdi, int32_t *tdo, int32_t *trst);

#define WIFI_NETWORKS_MAX 10

typedef struct {
    char ssid[33];
    char pass[65];
    bool auto_connect;
} wifi_network_t;

esp_err_t nvs_config_get_networks_count(int32_t *count);
esp_err_t nvs_config_get_network(int32_t index, wifi_network_t *net);
esp_err_t nvs_config_set_network(int32_t index, const wifi_network_t *net);
esp_err_t nvs_config_add_network(const wifi_network_t *net);
esp_err_t nvs_config_delete_network(int32_t index);
