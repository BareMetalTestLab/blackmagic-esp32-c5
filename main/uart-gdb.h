#pragma once
#include <stdint.h>

/**
 * Start GDB server
 */
void uart_gdb_server_init(void);

/**
 * Checks if someone is connected to the GDB server
 * @return bool
 */
bool uart_gdb_connected(void);

/**
 * Send data
 * @param buffer data
 * @param size data size
 */
void uart_gdb_send(uint8_t* buffer, size_t size);
