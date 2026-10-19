#include "just_flasher.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <target.h>
#include "general.h"
#include "gdb-glue.h"
#include "target_internal.h"

#define TAG "just_flasher"

/* The target can be driven by the gdb server, the web flasher or the cloud job
 * worker at the same time, so every operation runs under this lock */
static StaticSemaphore_t s_lock_mem;
static SemaphoreHandle_t s_lock   = NULL;
static target_s*         s_target = NULL;
static bool              s_held   = false;  // lock + NoAckMode owned by the current operation
static bool              s_wrote  = false;  // flasher_write() was called, end() must flush

/* Reset the target through the gdb glue and wait for the gdb server
 * to let go of its attached targets */
static void flasher_gdb_reset(void)
{
    char cmd_reset[] = "$qRcmd,7265736574#37";
    gdb_glue_receive((uint8_t*) cmd_reset, sizeof(cmd_reset));

    int try = 5;
    while (target_list && try-- > 0)
    {
        ESP_LOGI(TAG, "Wait for target to detach...");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

/* Detach, hand the target back to the gdb server and release the lock */
static void flasher_release(void)
{
    if (s_target)
    {
        target_detach(s_target);
        s_target = NULL;
    }
    flasher_gdb_reset();
    gdb_glue_receive((uint8_t*) "\x04", 1);  // disable NoAckMode
    s_held = false;
    xSemaphoreGive(s_lock);
}

/* Release without touching the target: used when begin() failed before attach */
static void flasher_release_early(void)
{
    gdb_glue_receive((uint8_t*) "\x04", 1);  // disable NoAckMode
    s_held = false;
    xSemaphoreGive(s_lock);
}

JustFlasherError just_flasher_begin(bool use_swd, uint32_t lock_wait_ms)
{
    if (s_lock == NULL)
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem); // If first begin create Mutex

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(lock_wait_ms)) != pdTRUE)
        return JustFlasherErrBusy;

    s_target = NULL;
    s_wrote  = false;
    s_held   = true;

    char pkt_enable_noack[] = "$QStartNoAckMode#B0";
    gdb_glue_receive((uint8_t*) pkt_enable_noack, sizeof(pkt_enable_noack));  // enable NoAckMode

    flasher_gdb_reset();

    // Scan for targets
    ESP_LOGI(TAG, "Scanning via %s...", use_swd ? "SWD" : "JTAG");
    if (!(use_swd ? adiv5_swd_scan() : jtag_scan()))
    {
        ESP_LOGE(TAG, "No target found");
        flasher_release_early();
        return JustFlasherErrNoTarget;
    }
    ESP_LOGI(TAG, "Target found via %s", use_swd ? "SWD" : "JTAG");

    // Attach to target
    s_target = target_attach_n(1, NULL);
    if (!s_target)
    {
        ESP_LOGE(TAG, "Failed to attach to target");
        flasher_gdb_reset();
        flasher_release_early();
        return JustFlasherErrAttach;
    }

    // Halt the target
    target_halt_request(s_target);
    vTaskDelay(pdMS_TO_TICKS(100));

    target_halt_reason_e halt_reason = target_halt_poll(s_target, NULL);
    if (halt_reason == TARGET_HALT_RUNNING || halt_reason == TARGET_HALT_ERROR)
    {
        ESP_LOGE(TAG, "Failed to halt target");
        flasher_release();
        return JustFlasherErrHalt;
    }

    ESP_LOGI(TAG, "Target attached and halted");

    // Log target and flash region information for diagnostics
    if (!s_target->flash)
    {
        ESP_LOGE(TAG, "Target has no flash region");
    }
    else
    {
        ESP_LOGI(TAG,
                 "Target: core=%s driver=%s cpuid=%#08lx designer=%#04x part_id=%#04x",
                 s_target->core ? s_target->core : "unknown",
                 s_target->driver ? s_target->driver : "unknown",
                 (unsigned long) s_target->cpuid,
                 (unsigned) s_target->designer_code,
                 (unsigned) s_target->part_id);
        for (target_flash_s* it = s_target->flash; it; it = it->next)
        {
            ESP_LOGI(TAG,
                     "Flash region: start=%#08lx length=%zu bytes (%zu KiB) blocksize=%zu",
                     (unsigned long) it->start,
                     it->length,
                     it->length / 1024,
                     it->blocksize);
        }
    }

    return JustFlasherOk;
}

JustFlasherError just_flasher_erase(uint32_t addr, size_t length)
{
    if (s_target == NULL)
        return JustFlasherErrNoTarget;

    if (length == 0)
    {
        length = just_flasher_get_flash_size();
        if (length == 0)
        {
            ESP_LOGE(TAG, "No flash regions to erase");
            return JustFlasherErrErase;
        }
    }

    ESP_LOGI(TAG, "Erasing flash at 0x%08lX, size: %zu bytes", (unsigned long) addr, length);
    if (!target_flash_erase(s_target, addr, length))
    {
        ESP_LOGE(TAG, "Flash erase failed");
        return JustFlasherErrErase;
    }
    return JustFlasherOk;
}

JustFlasherError just_flasher_write(uint32_t addr, const void* data, size_t length)
{
    if (s_target == NULL)
        return JustFlasherErrNoTarget;

    if (!target_flash_write(s_target, addr, data, length))
        return JustFlasherErrWrite;

    s_wrote = true;
    return JustFlasherOk;
}

JustFlasherError just_flasher_read(uint32_t addr, void* buffer, size_t length)
{
    if (s_target == NULL)
        return JustFlasherErrNoTarget;

    if (target_mem32_read(s_target, buffer, addr, length))
        return JustFlasherErrRead;

    return JustFlasherOk;
}

size_t just_flasher_get_flash_size(void)
{
    if (s_target == NULL)
        return 0;

    size_t total = 0;
    for (target_flash_s* it = s_target->flash; it; it = it->next)
    {
        // Address bit test as in the original code: matches the STM32 main flash bank
        if (it->start & 0x08000000)
            total += it->length;
    }
    return total;
}

JustFlasherError just_flasher_end(void)
{
    if (s_target == NULL)
        return JustFlasherErrNoTarget;

    if (s_wrote && !target_flash_complete(s_target))
    {
        // Data is committed by the writes, original behavior is to warn only
        ESP_LOGW(TAG, "Flash complete operation returned false");
    }

    // Reset and resume the target
    target_reset(s_target);
    target_halt_resume(s_target, false);

    flasher_release();
    return JustFlasherOk;
}

void just_flasher_abort(void)
{
    if (!s_held)
        return;
    flasher_release();
}

const char* just_flasher_err_str(JustFlasherError err)
{
    switch (err)
    {
    case JustFlasherOk:
        return "ok";
    case JustFlasherErrBusy:
        return "device busy";
    case JustFlasherErrNoTarget:
        return "no target device found";
    case JustFlasherErrAttach:
        return "failed to attach to target";
    case JustFlasherErrHalt:
        return "failed to halt target";
    case JustFlasherErrErase:
        return "flash erase failed";
    case JustFlasherErrWrite:
        return "flash write failed";
    case JustFlasherErrRead:
        return "flash read failed";
    }
    return "unknown flasher error";
}
