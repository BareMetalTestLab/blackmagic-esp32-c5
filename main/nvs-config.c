#include <m-string.h>
#include "nvs.h"
#include "nvs-config.h"

#define WIFI_SSID_KEY "wifi_ssid"
#define WIFI_PASS_KEY "wifi_pass"
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

#define ESP_WIFI_DEFAULT_SSID CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_DEFAULT_PASS CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_DEFAULT_HOSTNAME "blackmagic"

esp_err_t nvs_config_set_ssid(const mstring_t* ssid) {
    esp_err_t err = ESP_FAIL;

    if(mstring_size(ssid) > 0 && mstring_size(ssid) <= 32) {
        err = nvs_save_string(WIFI_SSID_KEY, ssid);
    }

    return err;
}

esp_err_t nvs_config_set_pass(const mstring_t* pass) {
    esp_err_t err = ESP_FAIL;

    if(mstring_size(pass) == 0 || (mstring_size(pass) >= 8 && mstring_size(pass) <= 64)) {
        err = nvs_save_string(WIFI_PASS_KEY, pass);
    }

    return err;
}

esp_err_t nvs_config_set_hostname(const mstring_t* hostname) {
    esp_err_t err = ESP_FAIL;

    if(mstring_size(hostname) > 0 && mstring_size(hostname) <= 32) {
        err = nvs_save_string(WIFI_HOSTNAME, hostname);
    }

    return err;
}

esp_err_t nvs_config_get_ssid(mstring_t* ssid) {
    esp_err_t err = nvs_load_string(WIFI_SSID_KEY, ssid);

    if(err != ESP_OK) {
        mstring_set(ssid, ESP_WIFI_DEFAULT_SSID);
    }

    return err;
}

esp_err_t nvs_config_get_pass(mstring_t* pass) {
    esp_err_t err = nvs_load_string(WIFI_PASS_KEY, pass);

    if(err != ESP_OK) {
        mstring_set(pass, ESP_WIFI_DEFAULT_PASS);
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
