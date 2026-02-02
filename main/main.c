#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "network.h"
#include "network-gdb.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "gdb_main.h"
#include "gdb_if.h"
#include "platform.h"
#include "gdb-glue.h"

void gdb_application_thread(void *pvParameters)
{
    while (1)
    {
        SET_IDLE_STATE(false);
        while (gdb_target_running && cur_target)
        {
            gdb_poll_target();

            // Check again, as `gdb_poll_target()` may
            // alter these variables.
            if (!gdb_target_running || !cur_target)
                break;
            char c = gdb_if_getchar_to(0);

            if (c == '\x03' || c == '\x04')
                target_halt_request(cur_target);
#ifdef ENABLE_RTT
            else if (rtt_enabled)
                poll_rtt(cur_target);
#endif
            // platform_pace_poll();
        }

        SET_IDLE_STATE(true);
        const gdb_packet_s *const packet = gdb_packet_receive();
        // If port closed and target detached, stay idle
        if (packet->data[0] != '\x04' || cur_target)
            SET_IDLE_STATE(false);
        gdb_main(packet);
    }
}

void app_main(void)
{
    gdb_glue_init();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    network_init();
    network_gdb_server_init();

    xTaskCreate(&gdb_application_thread, "gdb_thread", 4096, NULL, 5, NULL);
}
