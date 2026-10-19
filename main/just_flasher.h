#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLASHER_DEFAULT_BASE_ADDR 0x08000000 // default ARM Cortex-M flash base

typedef enum
{
    JustFlasherOk = 0,
    JustFlasherErrBusy,     // another flash operation holds the target
    JustFlasherErrNoTarget, // scan found no target device
    JustFlasherErrAttach,   // failed to attach to the target
    JustFlasherErrHalt,     // failed to halt the target
    JustFlasherErrErase,    // flash erase failed
    JustFlasherErrWrite,    // flash write failed
    JustFlasherErrRead,     // flash read failed
} JustFlasherError;

/* Take exclusive ownership of the target and prepare it for an operation:
 * gdb-glue reset handshake, SWD/JTAG scan, attach, halt.
 * lock_wait_ms bounds the wait while another flash operation is running.
 * On any error the target is already released, flasher_abort() is not needed. */
JustFlasherError just_flasher_begin(bool use_swd, uint32_t lock_wait_ms);

/* Erase length bytes at addr, length == 0 erases the whole main flash bank */
JustFlasherError just_flasher_erase(uint32_t addr, size_t length);

/* Write a firmware chunk, can be called repeatedly for streaming */
JustFlasherError just_flasher_write(uint32_t addr, const void* data, size_t length);

/* Read length bytes at addr into buffer */
JustFlasherError just_flasher_read(uint32_t addr, void* buffer, size_t length);

/* Total size of the target's main flash bank regions */
size_t just_flasher_get_flash_size(void);

/* Finish the operation: flush writes, reset and resume the target, release it */
JustFlasherError just_flasher_end(void);

/* Abort after a failure: detach from the target and release it, safe to call twice */
void just_flasher_abort(void);

/* Short human-readable error text, fits both HTTP responses and cloud job reports */
const char* just_flasher_err_str(JustFlasherError err);
