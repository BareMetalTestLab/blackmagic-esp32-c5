#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Start serial network server on port 2347
 */
void network_serial_init(void);

/**
 * Check if someone is connected to the serial network
 * @return true if connected
 */
bool network_serial_connected(void);

/**
 * Send serial network data to connected client
 * @param buffer data to send
 * @param size data size
 */
void network_serial_send(uint8_t *buffer, size_t size);
