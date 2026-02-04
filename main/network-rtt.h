#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Start RTT server on port 2346
 */
void network_rtt_server_init(void);

/**
 * Check if someone is connected to the RTT server
 * @return true if connected
 */
bool network_rtt_connected(void);

/**
 * Send RTT data to connected client
 * @param buffer data to send
 * @param size data size
 */
void network_rtt_send(uint8_t *buffer, size_t size);
