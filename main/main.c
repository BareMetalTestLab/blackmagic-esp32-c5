#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "network.h"
#include "network-gdb.h"
#include "network-http.h"
#include "network-serial.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "gdb_main.h"
#include "gdb_if.h"
#include "platform.h"
#include "gdb-glue.h"
#include "nvs-config.h"

#ifdef ENABLE_RTT
#include "network-rtt.h"
#include "rtt.h"
#include "rtt_if_esp32.h"
#endif

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

    nvs_init();

    // Load pin configuration from NVS and apply to platform
    nvs_config_get_pins(&g_pin_tms_swdio, &g_pin_tck_swclk, &g_pin_tdi, &g_pin_tdo_swo, &g_pin_trst);

    // TODO: Add target serial (LP UART) TCP port 2347

    // TODO: Add 3 UARTs for gdb, rtt, serial
    // 1. (UART0) uart to usb: for gdb
    // 2. (UART1) uart to usb: for rtt
    // 3. (LP UART) uart to serial: for target serial output (optional)

    //        ----- <- (USB2TTL) - UART0 ->       gdb       <- TCP port 2345
    // USB - | HUB |<- (USB2TTL) - UART1 ->       rtt       <- TCP port 2346
    //        ----- <-    jtag/cdc       -> serial(LP UART) <- TCP port 2347

    // In release build jtag/cdc is not needed, so we can use cdc for serial output, because there are not enough uarts

    network_init();
    network_gdb_server_init();
    network_http_server_init();
    // Initialize LP UART (TX=GPIO5, RX=GPIO4) and start welcome task
    network_serial_init();

#ifdef ENABLE_RTT
    network_rtt_server_init();
#endif

    xTaskCreate(&gdb_application_thread, "gdb_thread", 4096, NULL, 5, NULL);
}
