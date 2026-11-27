#pragma once
#include "bmcloud-channel.h"
#include "m-string.h"
#include <stdbool.h>
#include <stdint.h>

/* --- Device websocket: presence, pushed jobs, firmware transfers ---
 * One binary protobuf frame per message (bmcloud-channel does the encoding).
 * The client reconnects on its own; the server replays pending jobs on
 * (re)connect, delivery is at-least-once — dedupe by job_id.
 *
 * Firmware transfers ride the same socket, strictly sequential and right
 * behind their Job frames, so the receiver is one byte stream and the job
 * loop pairs transfer N with job N. Cumulative acks pace the server window. */

typedef struct bmcloud_ws_t bmcloud_ws_t;

bmcloud_ws_t* bmcloud_ws_start(void);
void          bmcloud_ws_ensure(bmcloud_ws_t* ws, const mstring_t* host, const mstring_t* token);
void          bmcloud_ws_stop(bmcloud_ws_t* ws);

/* Take one pushed job; false when none are waiting */
bool bmcloud_ws_job_pop(bmcloud_ws_t* ws, BmChannelJobType* job, uint32_t timeout_ms);

/* Queue a job report: the sender task ships it and keeps it queued until
 * delivered, so a dropped connection does not lose the outcome */
void bmcloud_ws_job_report(bmcloud_ws_t* ws, uint64_t job_id, BmChannelJobStatusType status, const char* error);

/* Wait for the next TransferBegin since the previous successful wait;
 * false on timeout or a failed receiver */
bool bmcloud_ws_fw_transfer_wait_begin(bmcloud_ws_t* ws, uint32_t timeout_ms, BmChannelTransferInfoType* info);

/* Read received firmware bytes; 0 when nothing arrived within the timeout.
 * A zero timeout polls what is already buffered. */
size_t bmcloud_ws_fw_transfer_read(bmcloud_ws_t* ws, void* buf, size_t len, uint32_t timeout_ms);

/* TransferEnd seen and every received byte consumed */
bool bmcloud_ws_fw_transfer_is_done(bmcloud_ws_t* ws);

/* Receiver failure (stall, overrun, protocol violation); NULL while healthy.
 * Sticky: only a reconnect clears it — call bmcloud_ws_stop to force one. */
const char* bmcloud_ws_fw_transfer_error(bmcloud_ws_t* ws);

/* Resynchronize the receiver in place, keeping the connection: abandon a
 * half-consumed transfer (its late chunks are discarded, the server's send
 * window is released) and clear the failure. The next TransferBegin starts
 * clean. Prefer over stop() when the transport itself is healthy — a queued
 * report ships on the live socket ahead of any reconnect replay. */
void bmcloud_ws_fw_transfer_reset(bmcloud_ws_t* ws);

/* Ack cumulative bytes taken from the transfer; paces the server window */
void bmcloud_ws_fw_transfer_ack(bmcloud_ws_t* ws, uint32_t transfer_id, uint64_t received_bytes);
