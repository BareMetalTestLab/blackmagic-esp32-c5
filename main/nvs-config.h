#pragma once

#include <m-string.h>
#include <esp_err.h>

esp_err_t nvs_config_set_ssid(const mstring_t* ssid);
esp_err_t nvs_config_set_pass(const mstring_t* pass);
esp_err_t nvs_config_set_hostname(const mstring_t* hostname);

esp_err_t nvs_config_get_ssid(mstring_t* ssid);
esp_err_t nvs_config_get_pass(mstring_t* pass);
esp_err_t nvs_config_get_hostname(mstring_t* hostname);
