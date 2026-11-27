#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bmcloud device channel protocol (bmcloud-proto submodule): one binary
 * frame = one protobuf envelope, DeviceMessage out, ServerMessage in.
 * This library does the encoding and decoding only; the transport — the
 * websocket, queues, retries — lives with the caller. */

/* Major version of the protocol this build speaks (device.proto) */
#define BMCHANNEL_PROTOCOL_VERSION 1

/* Heartbeat payload; silence reads as offline on the server */
typedef enum
{
    BmChannelStateOnline = 1,
    BmChannelStateBusy   = 2,
    BmChannelStateError  = 3,
} BmChannelStateType;

/* FLASHING claims a job and refreshes the server-side watchdog; DONE / FAILED finish it */
typedef enum
{
    BmChannelJobFlashing = 1,
    BmChannelJobDone     = 2,
    BmChannelJobFailed   = 3,
} BmChannelJobStatusType;

/* What a decoded server frame carries */
typedef enum
{
    BmChannelServerAck,           // generic reply to one of our messages
    BmChannelServerError,         // protocol-level error, not a job error
    BmChannelServerJob,           // pushed job: flash this firmware
    BmChannelServerTransferBegin, // firmware download begins
    BmChannelServerTransferChunk, // one 8–16 KB slice of the firmware
    BmChannelServerTransferEnd,   // all chunks sent
    BmChannelServerOther,         // stream frames, not spoken by this firmware yet
} BmChannelServerKindType;

/* Decoded Job push; the firmware follows as a transfer on the same channel */
typedef struct
{
    uint64_t job_id;
    uint64_t firmware_id;
    uint64_t size;       // bytes
    uint8_t  sha256[32]; // digest of the image, verified against the flash content
} BmChannelJobType;

/* Decoded TransferBegin / TransferEnd */
typedef struct
{
    uint32_t transfer_id;
    uint64_t total_size; // bytes, Begin only
} BmChannelTransferInfoType;

/* Decoded server frame */
typedef struct
{
    BmChannelServerKindType kind;
    union
    {
        char for_kind[24 + 1]; // Ack: which of our messages is acknowledged
        char error[64 + 1];    // Error: short server text
        BmChannelJobType job;
        BmChannelTransferInfoType begin;
        struct
        {
            uint32_t       transfer_id;
            uint64_t       offset;
            const uint8_t* data;     // points into the decode buffer:
            size_t         data_len; // valid until the next bmchannel_decode call
        } chunk;
        BmChannelTransferInfoType end;
    } as;
} BmChannelServerMsgType;

/* Encoders return the frame length, or -1 when it does not fit the buffer */
int bmchannel_encode_capabilities(
    const char*        firmware_version,
    const char* const* peripherals,
    size_t             peripheral_count,
    uint8_t*           buf,
    size_t             size);
int bmchannel_encode_job_report(
    uint64_t               job_id,
    BmChannelJobStatusType status,
    const char*            error, // text for FAILED, NULL otherwise
    uint8_t*               buf,
    size_t                 size);
int bmchannel_encode_transfer_ack(
    uint32_t transfer_id,
    uint64_t received_bytes, // cumulative; paces the server's send window
    uint8_t* buf,
    size_t   size);

/* Decode one server frame; false when the frame is malformed */
bool bmchannel_decode(const uint8_t* buf, size_t len, BmChannelServerMsgType* out);
