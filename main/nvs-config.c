#include <m-string.h>
#include "nvs.h"
#include "nvs-config.h"
#include <string.h>

#define WIFI_HOSTNAME "wifi_hostname"

#define PIN_SWDIO_KEY "pin_swdio"
#define PIN_SWCLK_KEY "pin_swclk"
#define PIN_TDI_KEY   "pin_tdi"
#define PIN_TDO_KEY   "pin_tdo"
#define PIN_TRST_KEY  "pin_trst"

#define DEFAULT_PIN_SWDIO 23
#define DEFAULT_PIN_SWCLK 24
#define DEFAULT_PIN_TDI   28
#define DEFAULT_PIN_TDO   27
#define DEFAULT_PIN_TRST  25

#define ESP_WIFI_DEFAULT_HOSTNAME "blackmagic"

esp_err_t nvs_config_set_hostname(const mstring_t* hostname) {
    esp_err_t err = ESP_FAIL;

    if(mstring_size(hostname) > 0 && mstring_size(hostname) <= 32) {
        err = nvs_save_string(WIFI_HOSTNAME, hostname);
    }

    return err;
}

esp_err_t nvs_config_get_hostname(mstring_t* hostname) {
    esp_err_t err = nvs_load_string(WIFI_HOSTNAME, hostname);

    if(err != ESP_OK) {
        mstring_set(hostname, ESP_WIFI_DEFAULT_HOSTNAME);
    }

    return err;
}

esp_err_t nvs_config_set_pins(int32_t swdio, int32_t swclk, int32_t tdi, int32_t tdo, int32_t trst) {
    nvs_save_i32(PIN_SWDIO_KEY, swdio);
    nvs_save_i32(PIN_SWCLK_KEY, swclk);
    nvs_save_i32(PIN_TDI_KEY,   tdi);
    nvs_save_i32(PIN_TDO_KEY,   tdo);
    nvs_save_i32(PIN_TRST_KEY,  trst);
    return ESP_OK;
}

esp_err_t nvs_config_get_pins(int32_t *swdio, int32_t *swclk, int32_t *tdi, int32_t *tdo, int32_t *trst) {
    if(nvs_load_i32(PIN_SWDIO_KEY, swdio) != ESP_OK) *swdio = DEFAULT_PIN_SWDIO;
    if(nvs_load_i32(PIN_SWCLK_KEY, swclk) != ESP_OK) *swclk = DEFAULT_PIN_SWCLK;
    if(nvs_load_i32(PIN_TDI_KEY,   tdi)   != ESP_OK) *tdi   = DEFAULT_PIN_TDI;
    if(nvs_load_i32(PIN_TDO_KEY,   tdo)   != ESP_OK) *tdo   = DEFAULT_PIN_TDO;
    if(nvs_load_i32(PIN_TRST_KEY,  trst)  != ESP_OK) *trst  = DEFAULT_PIN_TRST;
    return ESP_OK;
}

/* --- Known network list --- */

#define WIFI_NET_COUNT_KEY "net_count"

static void make_net_key(char *buf, int32_t index, const char *suffix) {
    snprintf(buf, 16, "n%ld_%s", (long)index, suffix);
}

esp_err_t nvs_config_get_networks_count(int32_t *count) {
    if(nvs_load_i32(WIFI_NET_COUNT_KEY, count) != ESP_OK) {
        *count = 0;
    }
    return ESP_OK;
}

esp_err_t nvs_config_get_network(int32_t index, wifi_network_t *net) {
    char key[16];
    mstring_t *s = mstring_alloc();

    make_net_key(key, index, "ssid");
    if(nvs_load_string(key, s) == ESP_OK) {
        strncpy(net->ssid, mstring_get_cstr(s), sizeof(net->ssid) - 1);
        net->ssid[sizeof(net->ssid) - 1] = '\0';
    } else {
        net->ssid[0] = '\0';
    }

    make_net_key(key, index, "pass");
    if(nvs_load_string(key, s) == ESP_OK) {
        strncpy(net->pass, mstring_get_cstr(s), sizeof(net->pass) - 1);
        net->pass[sizeof(net->pass) - 1] = '\0';
    } else {
        net->pass[0] = '\0';
    }

    int32_t auto_val = 0;
    make_net_key(key, index, "auto");
    nvs_load_i32(key, &auto_val);
    net->auto_connect = (auto_val != 0);

    mstring_free(s);
    return ESP_OK;
}

esp_err_t nvs_config_set_network(int32_t index, const wifi_network_t *net) {
    char key[16];
    mstring_t *s = mstring_alloc();

    make_net_key(key, index, "ssid");
    mstring_set(s, net->ssid);
    nvs_save_string(key, s);

    make_net_key(key, index, "pass");
    mstring_set(s, net->pass);
    nvs_save_string(key, s);

    make_net_key(key, index, "auto");
    nvs_save_i32(key, net->auto_connect ? 1 : 0);

    mstring_free(s);
    return ESP_OK;
}

esp_err_t nvs_config_add_network(const wifi_network_t *net) {
    int32_t count = 0;
    nvs_config_get_networks_count(&count);
    if(count >= WIFI_NETWORKS_MAX) return ESP_ERR_NO_MEM;

    nvs_config_set_network(count, net);
    nvs_save_i32(WIFI_NET_COUNT_KEY, count + 1);
    return ESP_OK;
}

esp_err_t nvs_config_delete_network(int32_t index) {
    int32_t count = 0;
    nvs_config_get_networks_count(&count);
    if(index < 0 || index >= count) return ESP_ERR_INVALID_ARG;

    for(int32_t i = index; i < count - 1; i++) {
        wifi_network_t n;
        nvs_config_get_network(i + 1, &n);
        nvs_config_set_network(i, &n);
    }
    nvs_save_i32(WIFI_NET_COUNT_KEY, count - 1);
    return ESP_OK;
}
