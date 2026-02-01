#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "general.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include "esp_timer.h"

#include <hal/gpio_ll.h>
#include <esp_rom_gpio.h>
#include <led_strip.h>


uint32_t swd_delay_cnt = 0;
uint32_t target_clk_divider = 0;

// static const char* TAG = "gdb-platform";

void __attribute__((always_inline)) platform_swdio_mode_float(void)
{
    gpio_ll_output_disable(&GPIO, SWDIO_PIN);
    gpio_ll_input_enable(&GPIO, SWDIO_PIN);
}

void __attribute__((always_inline)) platform_swdio_mode_drive(void)
{
    GPIO.enable_w1ts.val = (0x1 << SWDIO_PIN);
    esp_rom_gpio_connect_out_signal(SWDIO_PIN, SIG_GPIO_OUT_IDX, false, false);
    GPIO.enable_w1ts.val = (0x1 << SWCLK_PIN);
    esp_rom_gpio_connect_out_signal(SWCLK_PIN, SIG_GPIO_OUT_IDX, false, false);
}

void __attribute__((always_inline)) platform_gpio_set_level(int32_t gpio_num, uint32_t value)
{
    if (value)
    {
        GPIO.out_w1ts.val = (1 << gpio_num);
    }
    else
    {
        GPIO.out_w1tc.val = (1 << gpio_num);
    }
}

void __attribute__((always_inline)) platform_gpio_set(int32_t gpio_num)
{
    GPIO.out_w1ts.val = (1 << gpio_num);
}

void __attribute__((always_inline)) platform_gpio_clear(int32_t gpio_num)
{
    GPIO.out_w1tc.val = (1 << gpio_num);
}

int __attribute__((always_inline)) platform_gpio_get_level(int32_t gpio_num)
{
    int level = (GPIO.in.val >> gpio_num) & 0x1;
    return level;
}

// init platform
void platform_init()
{
}

// set reset target pin level
void platform_srst_set_val(bool assert)
{
    (void)assert;
}

// get reset target pin level
bool platform_srst_get_val(void)
{
    return false;
}

// target voltage
const char *platform_target_voltage(void)
{
    return "Unknown";
}

// platform time counter
uint32_t platform_time_ms(void)
{
    int64_t time_milli = esp_timer_get_time() / 1000;
    return ((uint32_t)time_milli);
}

// delay ms
void platform_delay(uint32_t ms)
{
    vTaskDelay((ms) / portTICK_PERIOD_MS);
}

// hardware version
int platform_hwversion(void)
{
    return 0;
}

// set timeout
void platform_timeout_set(platform_timeout_s *t, uint32_t ms)
{
    t->time = platform_time_ms() + ms;
}

// check timeout
bool platform_timeout_is_expired(const platform_timeout_s *t)
{
    return platform_time_ms() > t->time;
}

// set interface freq
void platform_max_frequency_set(uint32_t freq)
{
}

// get interface freq
uint32_t platform_max_frequency_get(void)
{
    return 0;
}

void platform_nrst_set_val(bool assert)
{
    (void)assert;
}

bool platform_nrst_get_val()
{
    return false;
}

void platform_target_clk_output_enable(bool enable)
{
    (void)enable;
}

/* SPI */
bool platform_spi_init(const spi_bus_e bus)
{
    (void)bus;
    return false;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
    (void)bus;
    return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
    (void)device_select;
    return false;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
    (void)bus;
    return value;
}

/* Serial */
void usb_serial_set_config(usbd_device *dev, uint16_t value)
{
    (void)dev;
    (void)value;
}

bool gdb_serial_get_dtr(void)
{
    return false;
}

void debug_serial_run(void)
{
}

uint32_t debug_serial_fifo_send(const char *fifo, uint32_t fifo_begin, uint32_t fifo_end)
{
    return 0;
}

void debug_serial_send_stdout(const uint8_t *const data, const size_t len)
{
    (void)data;
    (void)len;
}

bool onboard_flash_scan(void)
{
    return false;
}
