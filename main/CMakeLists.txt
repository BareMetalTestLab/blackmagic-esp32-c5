idf_component_register(SRCS "network.c" "main.c" "network-gdb.c" "network-rtt.c"
                    PRIV_REQUIRES esp_wifi nvs_flash
                    INCLUDE_DIRS ".")
