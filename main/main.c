#include <stdint.h>
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
#ifdef ENABLE_UART_SERIAL
#    include "uart-serial.h"
#endif
#ifdef ENABLE_USB_GDB
#    include "uart-gdb.h"
#    include "uart-rtt.h"
#endif
#include <hal/gpio_ll.h>
#include <driver/gpio.h>

#include "gdb_main.h"
#include "gdb_if.h"
#include "platform.h"
#include "gdb-glue.h"
#include "nvs-config.h"

#ifdef ENABLE_RTT
#    include "network-rtt.h"
#    include "rtt.h"
#    include "rtt_if_esp32.h"
#endif

#define MD_PIN 7
#define IDLE_PIN 8

void support_gpio_init(void)
{
    uint32_t output_mask = 0;
    if (MD_PIN >= 0)
        output_mask |= (1U << MD_PIN);
    if (IDLE_PIN >= 0)
        output_mask |= (1U << IDLE_PIN);

    if (output_mask != 0)
    {
        GPIO.enable_w1ts.val = output_mask;
    }

    if (MD_PIN >= 0)
    {
        esp_rom_gpio_connect_out_signal(MD_PIN, SIG_GPIO_OUT_IDX, false, false);
        platform_gpio_set_level(MD_PIN, 0);
    }
    if (IDLE_PIN >= 0)
    {
        esp_rom_gpio_connect_out_signal(IDLE_PIN, SIG_GPIO_OUT_IDX, false, false);
        platform_gpio_set_level(IDLE_PIN, 0);
    }
}

void gdb_application_thread(void* pvParameters)
{
    while (1)
    {
        platform_gpio_set_level(IDLE_PIN, 0);
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

        platform_gpio_set_level(IDLE_PIN, 1);
        const gdb_packet_s* const packet = gdb_packet_receive();
        // If port closed and target detached, stay idle
        if (packet->data[0] != '\x04' || cur_target)
            platform_gpio_set_level(IDLE_PIN, 0);
        gdb_main(packet);
    }
}

void app_main(void)
{
    gdb_glue_init();
    support_gpio_init();

    nvs_init();

    // Load pin configuration from NVS and apply to platform
    nvs_config_get_pins(&g_pin_tms_swdio, &g_pin_tck_swclk, &g_pin_tdi, &g_pin_tdo_swo, &g_pin_trst);

    // TODO: Add 3 UARTs for gdb, rtt, serial
    // 1. (UART0) uart to usb: for gdb
    // 2. (UART1) uart to usb: for rtt
    // 3. (LP UART) uart to serial: for target serial output (optional)

    //        ----- <-    jtag/cdc       ->       gdb       <- TCP port 2345
    // USB - | HUB |<- (USB2TTL) - UART1 ->       rtt       <- TCP port 2346
    //        ----- <- (USB2TTL) - UART0 -> serial(LP UART) <- TCP port 2347

    // In release build jtag/cdc is not needed, so we can use cdc for serial output, because there are not enough uarts
    if (network_start() == 0)
    {
        platform_gpio_set_level(MD_PIN, 0);
    }
    else
    {
        platform_gpio_set_level(MD_PIN, 1);
    }
    network_gdb_server_init();
#ifdef ENABLE_USB_GDB
    // GDB server on JTAG/CDC, UART0 port as the console
    uart_gdb_server_init();
    // RTT server on UART1
    uart_rtt_server_init();
#endif
    network_http_server_init();
#ifdef ENABLE_UART_SERIAL
    // Serial bridge UART0 <-> LP UART (replaces the TCP serial server on port 2347)
    uart_serial_init();
#else
    // Serial bridge TCP 2347 <-> LP UART
    network_serial_init();
#endif

#ifdef ENABLE_RTT
    network_rtt_server_init();
#endif

    xTaskCreate(&gdb_application_thread, "gdb_thread", 4096, NULL, 5, NULL);
}
