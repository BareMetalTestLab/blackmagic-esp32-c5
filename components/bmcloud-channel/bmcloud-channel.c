#include "bmcloud-channel.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include <stdio.h>
#include <string.h>

#include "bmcloud/device.pb.h"

/* Copy into a bounded nanopb char field; always terminated, never overruns */
static void copy_bounded(char* dst, size_t dst_size, const char* src)
{
    if (src == NULL)
        src = "";
    snprintf(dst, dst_size, "%s", src);
}

static int encode_envelope(bmcloud_DeviceMessage* msg, uint8_t* buf, size_t size)
{
    pb_ostream_t stream = pb_ostream_from_buffer(buf, size);
    if (!pb_encode(&stream, bmcloud_DeviceMessage_fields, msg))
        return -1;
    return (int) stream.bytes_written;
}

int bmchannel_encode_capabilities(const char*        firmware_version,
                                  const char* const* peripherals,
                                  size_t             peripheral_count,
                                  uint8_t*           buf,
                                  size_t             size)
{
    bmcloud_DeviceMessage msg             = bmcloud_DeviceMessage_init_zero;
    msg.which_msg                         = bmcloud_DeviceMessage_capabilities_tag;
    msg.msg.capabilities.protocol_version = BMCHANNEL_PROTOCOL_VERSION;

    // the nanopb.options bound this to 16 entries of 16 chars each: take what fits
    bmcloud_Capabilities* caps = &msg.msg.capabilities;
    copy_bounded(caps->firmware_version, sizeof(caps->firmware_version), firmware_version);
    if (peripheral_count > sizeof(caps->peripherals) / sizeof(caps->peripherals[0]))
        peripheral_count = sizeof(caps->peripherals) / sizeof(caps->peripherals[0]);
    caps->peripherals_count = (pb_size_t) peripheral_count;
    for (size_t i = 0; i < peripheral_count; i++)
        copy_bounded(caps->peripherals[i], sizeof(caps->peripherals[i]), peripherals[i]);

    return encode_envelope(&msg, buf, size);
}

int bmchannel_encode_job_report(uint64_t               job_id,
                                BmChannelJobStatusType status,
                                const char*            error,
                                uint8_t*               buf,
                                size_t                 size)
{
    bmcloud_DeviceMessage msg = bmcloud_DeviceMessage_init_zero;
    msg.which_msg             = bmcloud_DeviceMessage_job_report_tag;
    msg.msg.job_report.job_id = job_id;
    msg.msg.job_report.status = (bmcloud_JobReport_Status) status;
    if (error != NULL)
        copy_bounded(msg.msg.job_report.error, sizeof(msg.msg.job_report.error), error);

    return encode_envelope(&msg, buf, size);
}

int bmchannel_encode_transfer_ack(uint32_t transfer_id, uint64_t received_bytes, uint8_t* buf, size_t size)
{
    bmcloud_DeviceMessage msg           = bmcloud_DeviceMessage_init_zero;
    msg.which_msg                       = bmcloud_DeviceMessage_transfer_ack_tag;
    msg.msg.transfer_ack.transfer_id    = transfer_id;
    msg.msg.transfer_ack.received_bytes = received_bytes;

    return encode_envelope(&msg, buf, size);
}

/* Zero-copy view of a TransferChunk payload: the data field has no max_size
 * in nanopb.options, so nanopb hands it to this callback. The decode input is
 * a contiguous buffer, so the span is recorded where the bytes already sit
 * and simply skipped. Multiple invocations extend the same span. */
typedef struct
{
    const uint8_t* start;
    size_t         len;
} chunk_span_t;

static bool chunk_peek(pb_istream_t* stream, const pb_field_t* field, void** arg)
{
    (void) field;
    chunk_span_t* span = (chunk_span_t*) *arg;

    size_t         n    = stream->bytes_left;
    const uint8_t* here = (const uint8_t*) stream->state;

    if (span->start == NULL)
    {
        span->start = here;
        span->len   = n;
    }
    else if (here == span->start + span->len)
    {
        span->len += n;
    }
    else
    {
        return false;  // not contiguous: refuse rather than report a bogus span
    }

    return pb_read(stream, NULL, n);  // consume without copying
}

/* nanopb wipes a oneof member before decoding it, so a callback set inside
 * the union does not survive. The submsg_callback option gives ServerMessage
 * a message-level hook that runs after the wipe and before the fields: arm
 * the payload callback there. */
static bool chunk_arm(pb_istream_t* stream, const pb_field_t* field, void** arg)
{
    (void) stream;
    bmcloud_TransferChunk* chunk = (bmcloud_TransferChunk*) field->pData;
    chunk->data.funcs.decode     = chunk_peek;
    chunk->data.arg              = *arg;
    return true;
}

bool bmchannel_decode(const uint8_t* buf, size_t len, BmChannelServerMsgType* out)
{
    if (buf == NULL || out == NULL)
        return false;

    // with the data field as a callback the whole message fits on the stack
    bmcloud_ServerMessage bm = bmcloud_ServerMessage_init_zero;

    chunk_span_t span      = {0};
    bm.cb_msg.funcs.decode = chunk_arm;
    bm.cb_msg.arg          = &span;

    pb_istream_t stream = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&stream, bmcloud_ServerMessage_fields, &bm))
        return false;

    switch (bm.which_msg)
    {
    case bmcloud_ServerMessage_ack_tag:
        out->kind = BmChannelServerAck;
        copy_bounded(out->as.for_kind, sizeof(out->as.for_kind), bm.msg.ack.for_kind);
        break;

    case bmcloud_ServerMessage_error_tag:
        out->kind = BmChannelServerError;
        copy_bounded(out->as.error, sizeof(out->as.error), bm.msg.error.text);
        break;

    case bmcloud_ServerMessage_job_tag:
        out->kind = BmChannelServerJob;
        // a job without its firmware half is a contract violation, not a job
        if (!bm.msg.job.has_firmware)
            return false;
        // a short digest would fake a post-flash mismatch: refuse it here
        if (bm.msg.job.firmware.sha256.size != sizeof(out->as.job.sha256))
            return false;
        out->as.job.job_id      = bm.msg.job.job_id;
        out->as.job.firmware_id = bm.msg.job.firmware.firmware_id;
        out->as.job.size        = bm.msg.job.firmware.size;
        memcpy(out->as.job.sha256, bm.msg.job.firmware.sha256.bytes, sizeof(out->as.job.sha256));
        break;

    case bmcloud_ServerMessage_transfer_begin_tag:
        out->kind                 = BmChannelServerTransferBegin;
        out->as.begin.transfer_id = bm.msg.transfer_begin.transfer_id;
        out->as.begin.total_size  = bm.msg.transfer_begin.total_size;
        break;

    case bmcloud_ServerMessage_transfer_chunk_tag:
        out->kind                 = BmChannelServerTransferChunk;
        out->as.chunk.transfer_id = bm.msg.transfer_chunk.transfer_id;
        out->as.chunk.offset      = bm.msg.transfer_chunk.offset;
        // points into the decode input; valid until the next decode call
        out->as.chunk.data     = span.start != NULL ? span.start : buf;
        out->as.chunk.data_len = span.len;
        break;

    case bmcloud_ServerMessage_transfer_end_tag:
        out->kind               = BmChannelServerTransferEnd;
        out->as.end.transfer_id = bm.msg.transfer_end.transfer_id;
        break;

    default:
        // unknown kinds are skipped per the contract: streams and transfers arrive here
        out->kind = BmChannelServerOther;
        break;
    }

    return true;
}
