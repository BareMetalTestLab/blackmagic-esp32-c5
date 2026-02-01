#pragma once
#include <stdint.h>

/**
 * Start GDB server
 */
void network_gdb_server_init(void);

/**
 * Checks if someone is connected to the GDB server
 * @return bool
 */
bool network_gdb_connected(void);

/**
 * Send data
 * @param buffer data
 * @param size data size
 */
void network_gdb_send(uint8_t* buffer, size_t size);
