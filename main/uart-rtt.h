#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Start RTT server on UART1
 */
void uart_rtt_server_init(void);

/**
 * Checks if someone is connected to the RTT server
 * @return true if connected
 */
bool uart_rtt_connected(void);

/**
 * Send RTT data to connected client
 * @param buffer data to send
 * @param size data size
 */
void uart_rtt_send(uint8_t *buffer, size_t size);
