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

int bmchannel_encode_capabilities(
    const char*        firmware_version,
    const char* const* peripherals,
    size_t             peripheral_count,
    uint8_t*           buf,
    size_t             size)
{
    bmcloud_DeviceMessage msg = bmcloud_DeviceMessage_init_zero;
    msg.which_msg           = bmcloud_DeviceMessage_capabilities_tag;
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

int bmchannel_encode_job_report(
    uint64_t               job_id,
    BmChannelJobStatusType status,
    const char*            error,
    uint8_t*               buf,
    size_t                 size)
{
    bmcloud_DeviceMessage msg = bmcloud_DeviceMessage_init_zero;
    msg.which_msg        = bmcloud_DeviceMessage_job_report_tag;
    msg.msg.job_report.job_id = job_id;
    msg.msg.job_report.status = (bmcloud_JobReport_Status) status;
    if (error != NULL)
        copy_bounded(msg.msg.job_report.error, sizeof(msg.msg.job_report.error), error);

    return encode_envelope(&msg, buf, size);
}

bool bmchannel_decode(const uint8_t* buf, size_t len, BmChannelServerMsgType* out)
{
    if (buf == NULL || out == NULL)
        return false;

    // TransferChunk.data makes this struct roughly 16 KB. Keeping it in a
    // static buffer avoids stack pressure and repeated 16 KB heap allocations.
    static bmcloud_ServerMessage server;
    memset(&server, 0, sizeof(server));

    pb_istream_t stream = pb_istream_from_buffer(buf, len);
    bool decoded = pb_decode(&stream, bmcloud_ServerMessage_fields, &server);
    if (!decoded)
        return false;

    switch (server.which_msg)
    {
    case bmcloud_ServerMessage_ack_tag:
        out->kind = BmChannelServerAck;
        copy_bounded(out->as.for_kind, sizeof(out->as.for_kind), server.msg.ack.for_kind);
        break;

    case bmcloud_ServerMessage_error_tag:
        out->kind = BmChannelServerError;
        copy_bounded(out->as.error, sizeof(out->as.error), server.msg.error.text);
        break;

    case bmcloud_ServerMessage_job_tag:
        out->kind = BmChannelServerJob;
        // a job without its firmware half is a contract violation, not a job
        if (!server.msg.job.has_firmware)
            return false;
        out->as.job.job_id      = server.msg.job.job_id;
        out->as.job.firmware_id = server.msg.job.firmware.firmware_id;
        out->as.job.size        = server.msg.job.firmware.size;
        out->as.job.has_sha256  = server.msg.job.firmware.sha256.size == sizeof(out->as.job.sha256);
        if (out->as.job.has_sha256)
            memcpy(out->as.job.sha256, server.msg.job.firmware.sha256.bytes, sizeof(out->as.job.sha256));
        break;

    default:
        // unknown kinds are skipped per the contract: streams and transfers arrive here
        out->kind = BmChannelServerOther;
        break;
    }

    return true;
}
