#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Initialize RTT interface
 * @return 0 on success, -1 on failure
 */
int rtt_if_init(void);

/**
 * Teardown RTT interface
 * @return 0 on success, -1 on failure
 */
int rtt_if_exit(void);

/**
 * Receive RTT data from host (network/USB)
 * This function is called by the network layer when RTT data is received
 * @param buffer data received from host
 * @param size data size
 */
void rtt_receive_data(const uint8_t *buffer, size_t size);

/**
 * Flush any pending RTT transmit data
 */
void rtt_flush(void);
