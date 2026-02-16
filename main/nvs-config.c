#include <m-string.h>
#include "nvs.h"
#include "nvs-config.h"

#define WIFI_SSID_KEY "wifi_ssid"
#define WIFI_PASS_KEY "wifi_pass"
#define WIFI_HOSTNAME "wifi_hostname"

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
