#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include "general.h"
#include "platform.h"
#include "rtt.h"
#include "rtt_if.h"
#include "rtt_if_esp32.h"

#define RTT_RX_BUFFER_SIZE 512
#define RTT_TX_BUFFER_SIZE 2048
#define TAG "rtt_if"

/*********************************************************************
*
*       RTT terminal I/O for ESP32 platform
*
**********************************************************************
*/

/* RTT receive buffer (host to target) */
static StreamBufferHandle_t rtt_rx_stream = NULL;
static bool rtt_rx_stream_full = false;

/* RTT transmit buffer (target to host) */
static uint8_t rtt_tx_buffer[RTT_TX_BUFFER_SIZE];
static size_t rtt_tx_buffer_index = 0;

/* External network interface - RTT uses separate port */
extern bool network_rtt_connected(void);
extern void network_rtt_send(uint8_t *buffer, size_t size);

/*********************************************************************
*
*       Initialization and teardown
*
**********************************************************************
*/

/* Initialize RTT interface */
int rtt_if_init(void)
{
	if (rtt_rx_stream == NULL) {
		rtt_rx_stream = xStreamBufferCreate(RTT_RX_BUFFER_SIZE, 1);
		if (rtt_rx_stream == NULL) {
			ESP_LOGE(TAG, "Failed to create RTT RX stream");
			return -1;
		}
	}
	rtt_rx_stream_full = false;
	rtt_tx_buffer_index = 0;
	ESP_LOGI(TAG, "RTT interface initialized");
	return 0;
}

/* Teardown RTT interface */
int rtt_if_exit(void)
{
	if (rtt_rx_stream != NULL) {
		vStreamBufferDelete(rtt_rx_stream);
		rtt_rx_stream = NULL;
	}
	rtt_rx_stream_full = false;
	rtt_tx_buffer_index = 0;
	ESP_LOGI(TAG, "RTT interface deinitialized");
	return 0;
}

/*********************************************************************
*
*       RTT from host to target
*
**********************************************************************
*/

/* Receive data from host (called by network/USB layer) */
void rtt_receive_data(const uint8_t *buffer, size_t size)
{
	if (rtt_rx_stream == NULL || buffer == NULL || size == 0)
		return;

	size_t space_available = xStreamBufferSpacesAvailable(rtt_rx_stream);

	/* Skip or handle flow control based on flags */
	if (rtt_flag_skip && size > space_available) {
		ESP_LOGW(TAG, "RTT RX buffer full, dropping data (skip mode)");
		return;
	}

	if (rtt_flag_block && size > space_available) {
		ESP_LOGW(TAG, "RTT RX buffer full (block mode)");
		rtt_rx_stream_full = true;
		return;
	}

	/* Send data to stream buffer */
	size_t sent = xStreamBufferSend(rtt_rx_stream, buffer, size, 0);
	if (sent < size) {
		ESP_LOGW(TAG, "RTT RX buffer overflow, dropped %d bytes", size - sent);
	}
}

/* Host to target: read one character from the channel, non-blocking */
int32_t rtt_getchar(const uint32_t channel)
{
	/* Only support reading from down channel 0 */
	if (channel != 0U)
		return -1;

	if (rtt_rx_stream == NULL)
		return -1;

	uint8_t data;
	size_t received = xStreamBufferReceive(rtt_rx_stream, &data, 1, 0);

	if (received == 0)
		return -1;

	/* Check if we can unblock flow control */
	if (rtt_rx_stream_full && xStreamBufferSpacesAvailable(rtt_rx_stream) >= 64) {
		rtt_rx_stream_full = false;
		ESP_LOGD(TAG, "RTT RX stream freed");
	}

	return data;
}

/* Host to target: true if no characters available for reading */
bool rtt_nodata(const uint32_t channel)
{
	/* Only support reading from down channel 0 */
	if (channel != 0U)
		return true;

	if (rtt_rx_stream == NULL)
		return true;

	return xStreamBufferIsEmpty(rtt_rx_stream);
}

/*********************************************************************
*
*       RTT from target to host
*
**********************************************************************
*/

/* Target to host: write string */
uint32_t rtt_write(const uint32_t channel, const char *buf, uint32_t len)
{
	/* Only support writing to up channel 0 */
	if (channel != 0U)
		return len;

	if (buf == NULL || len == 0)
		return 0;

	/* Send data over network if connected */
	if (network_rtt_connected()) {
		/* Buffer the data */
		for (uint32_t i = 0; i < len; i++) {
			rtt_tx_buffer[rtt_tx_buffer_index++] = buf[i];

			/* Flush buffer if full or on newline */
			if (rtt_tx_buffer_index >= RTT_TX_BUFFER_SIZE - 1 || buf[i] == '\n') {
				network_rtt_send(rtt_tx_buffer, rtt_tx_buffer_index);
				rtt_tx_buffer_index = 0;
			}
		}
	} else {
		/* No connection, just discard or log */
		ESP_LOGD(TAG, "RTT write: no connection, %d bytes discarded", len);
	}

	return len;
}

/* Flush any pending RTT transmit data */
void rtt_flush(void)
{
	if (rtt_tx_buffer_index > 0 && network_rtt_connected()) {
		network_rtt_send(rtt_tx_buffer, rtt_tx_buffer_index);
		rtt_tx_buffer_index = 0;
	}
}
