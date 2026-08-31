#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize the serial bridge UART0 <-> LP UART and start the bridge tasks
 *
 * Installs the UART drivers on both ports (LP UART uses the fixed pins
 * TX=GPIO5, RX=GPIO4) and starts two tasks forwarding data in both
 * directions.
 */
void uart_serial_init(void);

/**
 * @brief Check if the serial bridge is running
 *
 * @return true when both UART drivers are installed and the bridge tasks are active
 */
bool uart_serial_connected(void);

/**
 * @brief Send data from the LP UART side to UART0
 *
 * @param buffer Data to send
 * @param size Number of bytes to send
 */
void uart_serial_send(uint8_t *buffer, size_t size);
