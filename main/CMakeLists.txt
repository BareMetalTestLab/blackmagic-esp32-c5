idf_component_register(SRCS "network.c" "main.c" "network-gdb.c" "network-http.c" "network-rtt.c"
                    PRIV_REQUIRES esp_wifi esp_https_server nvs_flash
                    INCLUDE_DIRS ".")
