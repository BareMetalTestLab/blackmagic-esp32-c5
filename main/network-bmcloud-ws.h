#pragma once
#include "bmcloud-channel.h"
#include "m-string.h"
#include <stdbool.h>
#include <stdint.h>

/* --- Device websocket: presence, pushed jobs, job reports ---
 * One binary protobuf frame per message (bmcloud-channel does the encoding).
 * The client reconnects on its own; the server replays pending jobs on
 * (re)connect, delivery is at-least-once — dedupe by job_id. */

typedef struct bmcloud_ws_t bmcloud_ws_t;

bmcloud_ws_t* bmcloud_ws_start(void);
void          bmcloud_ws_ensure(bmcloud_ws_t* ws, const mstring_t* host, const mstring_t* token);
void          bmcloud_ws_stop(bmcloud_ws_t* ws);

/* Take one pushed job; false when none are waiting */
bool bmcloud_ws_pop_job(bmcloud_ws_t* ws, BmChannelJobType* job);

/* Queue a job report: the sender task ships it and keeps it queued until
 * delivered, so a dropped connection does not lose the outcome */
void bmcloud_ws_report_job(bmcloud_ws_t* ws, uint64_t job_id, BmChannelJobStatusType status, const char* error);
