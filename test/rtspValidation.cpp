#include "rtspValidation.h"
#include "../rtspFrameId.h"

#include <gst/gst.h>
#include <string.h>

#define H265_NAL_PREFIX_SEI 39
#define H265_NAL_SUFFIX_SEI 40
#define H265_SEI_REGISTERED_USER_DATA 4

static gboolean find_start_code(const guint8 *data, gsize size, gsize from,
                                gsize *position, gsize *length)
{
    if (!data || !position || !length || from >= size)
        return FALSE;

    for (gsize i = from; i + 3 <= size; i++)
    {
        if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 0 && data[i + 3] == 1)
        {
            *position = i;
            *length = 4;
            return TRUE;
        }
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
        {
            *position = i;
            *length = 3;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean unescape_rbsp(const guint8 *data, gsize size,
                              GByteArray *rbsp)
{
    if (!data || !rbsp)
        return FALSE;

    guint zero_count = 0;
    for (gsize i = 0; i < size; i++)
    {
        guint8 value = data[i];
        if (zero_count >= 2 && value == 0x03)
        {
            if (i + 1 >= size || data[i + 1] > 0x03)
                return FALSE;
            zero_count = 0;
            continue;
        }

        g_byte_array_append(rbsp, &value, 1);
        zero_count = value == 0 ? zero_count + 1 : 0;
    }
    return TRUE;
}

static gboolean parse_ff_value(const guint8 *data, gsize size, gsize *offset,
                               guint *value)
{
    if (!data || !offset || !value)
        return FALSE;

    guint result = 0;
    while (*offset < size && data[*offset] == 0xff)
    {
        if (result > G_MAXUINT - 0xff)
            return FALSE;
        result += 0xff;
        (*offset)++;
    }
    if (*offset >= size || result > G_MAXUINT - data[*offset])
        return FALSE;
    result += data[*offset];
    (*offset)++;
    *value = result;
    return TRUE;
}

static gboolean trailing_bits_valid(const guint8 *data, gsize size,
                                    gsize offset)
{
    if (offset >= size || data[offset] != 0x80)
        return FALSE;
    for (gsize i = offset + 1; i < size; i++)
    {
        if (data[i] != 0)
            return FALSE;
    }
    return TRUE;
}

static RtspFrameIdParseStatus parse_sei_rbsp(
    const guint8 *data, gsize size, guint8 expected_channel,
    RtspFrameIdRecord *record)
{
    gsize offset = 0;
    RtspFrameIdParseStatus candidate_error = RTSP_FRAME_ID_PARSE_NOT_FOUND;
    gboolean found = FALSE;
    RtspFrameIdRecord candidate_record = {};

    while (offset < size)
    {
        if (data[offset] == 0x80)
        {
            if (!trailing_bits_valid(data, size, offset))
                return RTSP_FRAME_ID_PARSE_MALFORMED;
            if (found)
                *record = candidate_record;
            return candidate_error;
        }

        guint payload_type = 0;
        guint payload_size = 0;
        if (!parse_ff_value(data, size, &offset, &payload_type) ||
            !parse_ff_value(data, size, &offset, &payload_size) ||
            payload_size > size - offset)
            return RTSP_FRAME_ID_PARSE_MALFORMED;

        const guint8 *payload = data + offset;
        offset += payload_size;
        if (payload_type != H265_SEI_REGISTERED_USER_DATA)
            continue;
        if (payload_size < 2)
            return RTSP_FRAME_ID_PARSE_MALFORMED;
        if (payload[0] != RTSP_FRAME_ID_COUNTRY_CODE ||
            payload[1] != RTSP_FRAME_ID_COUNTRY_EXTENSION)
        {
            if (payload_size >= 2 + RTSP_FRAME_ID_MAGIC_SIZE &&
                memcmp(payload + 2, RTSP_FRAME_ID_MAGIC,
                       RTSP_FRAME_ID_MAGIC_SIZE) == 0)
                return RTSP_FRAME_ID_PARSE_WRONG_IDENTITY;
            continue;
        }

        if (payload_size != RTSP_FRAME_ID_REGISTERED_PAYLOAD_SIZE)
        {
            return RTSP_FRAME_ID_PARSE_MALFORMED;
        }

        const guint8 *frame_data = payload + 2;
        if (memcmp(frame_data, RTSP_FRAME_ID_MAGIC,
                   RTSP_FRAME_ID_MAGIC_SIZE) != 0 ||
            frame_data[10] != 0 || frame_data[11] != 0)
        {
            return RTSP_FRAME_ID_PARSE_WRONG_IDENTITY;
        }
        if (frame_data[8] != RTSP_FRAME_ID_VERSION)
        {
            return RTSP_FRAME_ID_PARSE_WRONG_VERSION;
        }
        if (frame_data[9] != expected_channel)
        {
            return RTSP_FRAME_ID_PARSE_WRONG_CHANNEL;
        }

        if (found)
            return RTSP_FRAME_ID_PARSE_MALFORMED;
        candidate_record.version = frame_data[8];
        candidate_record.channel = frame_data[9];
        candidate_record.frame_id = GST_READ_UINT64_BE(frame_data + 12);
        candidate_record.origin_pts = GST_READ_UINT64_BE(frame_data + 20);
        found = TRUE;
        candidate_error = RTSP_FRAME_ID_PARSE_OK;
    }

    return RTSP_FRAME_ID_PARSE_MALFORMED;
}

gboolean rtsp_h265_au_contains_idr(const guint8 *data, gsize size,
                                   gboolean *contains_idr)
{
    if (!data || !contains_idr || size < 5)
        return FALSE;

    gboolean found_idr = FALSE;
    gsize start_position = 0;
    gsize start_length = 0;
    if (!find_start_code(data, size, 0, &start_position, &start_length))
        return FALSE;
    for (gsize leading = 0; leading < start_position; leading++)
    {
        if (data[leading] != 0)
            return FALSE;
    }

    while (TRUE)
    {
        const gsize nal_start = start_position + start_length;
        gsize next_position = 0;
        gsize next_length = 0;
        const gboolean has_next = find_start_code(
            data, size, nal_start, &next_position, &next_length);
        const gsize nal_end = has_next ? next_position : size;

        if (nal_end < nal_start + 2)
            return FALSE;

        const guint8 byte0 = data[nal_start];
        const guint8 byte1 = data[nal_start + 1];
        if ((byte0 & 0x80) != 0 || (byte1 & 0x07) == 0)
            return FALSE;

        const guint nal_type = (byte0 >> 1) & 0x3f;
        if (nal_type == 19 || nal_type == 20)
            found_idr = TRUE;

        if (!has_next)
            break;
        start_position = next_position;
        start_length = next_length;
    }

    *contains_idr = found_idr;
    return TRUE;
}

RtspFrameIdParseStatus rtsp_frame_id_parse_au(
    const guint8 *data, gsize size, guint8 expected_channel,
    RtspFrameIdRecord *record)
{
    if (!data || !record || size < 5)
        return RTSP_FRAME_ID_PARSE_MALFORMED;

    gboolean found = FALSE;
    gboolean has_vcl = FALSE;
    RtspFrameIdRecord candidate_record = {};
    gsize start_position = 0;
    gsize start_length = 0;
    if (!find_start_code(data, size, 0, &start_position, &start_length))
        return RTSP_FRAME_ID_PARSE_MALFORMED;

    for (gsize leading = 0; leading < start_position; leading++)
    {
        if (data[leading] != 0)
            return RTSP_FRAME_ID_PARSE_MALFORMED;
    }

    while (TRUE)
    {
        const gsize nal_start = start_position + start_length;
        gsize next_position = 0;
        gsize next_length = 0;
        const gboolean has_next =
            find_start_code(data, size, nal_start, &next_position,
                            &next_length);
        const gsize nal_end = has_next ? next_position : size;

        if (nal_end < nal_start + 2)
            return RTSP_FRAME_ID_PARSE_MALFORMED;
        else
        {
            const guint8 byte0 = data[nal_start];
            const guint8 byte1 = data[nal_start + 1];
            const guint nal_type = (byte0 >> 1) & 0x3f;
            if ((byte0 & 0x80) != 0 || (byte1 & 0x07) == 0)
                return RTSP_FRAME_ID_PARSE_MALFORMED;
            if (nal_type <= 31)
                has_vcl = TRUE;
            if (nal_type == H265_NAL_PREFIX_SEI ||
                nal_type == H265_NAL_SUFFIX_SEI)
            {
                GByteArray *rbsp = g_byte_array_sized_new(
                    nal_end - nal_start - 2);
                if (!unescape_rbsp(data + nal_start + 2,
                                   nal_end - nal_start - 2, rbsp))
                {
                    g_byte_array_unref(rbsp);
                    return RTSP_FRAME_ID_PARSE_MALFORMED;
                }

                RtspFrameIdRecord candidate = {};
                RtspFrameIdParseStatus status = parse_sei_rbsp(
                    rbsp->data, rbsp->len, expected_channel, &candidate);
                g_byte_array_unref(rbsp);
                if (status == RTSP_FRAME_ID_PARSE_OK)
                {
                    if (found)
                        return RTSP_FRAME_ID_PARSE_MALFORMED;
                    found = TRUE;
                    candidate_record = candidate;
                }
                else if (status != RTSP_FRAME_ID_PARSE_NOT_FOUND)
                    return status;
            }
        }

        if (!has_next)
            break;
        start_position = next_position;
        start_length = next_length;
    }

    if (found && !has_vcl)
        return RTSP_FRAME_ID_PARSE_MALFORMED;
    if (found)
    {
        *record = candidate_record;
        return RTSP_FRAME_ID_PARSE_OK;
    }
    return has_vcl ? RTSP_FRAME_ID_PARSE_NOT_FOUND
                   : RTSP_FRAME_ID_PARSE_NOT_FRAME;
}

RtspFrameOrder rtsp_frame_order_check(gboolean has_previous,
                                      guint64 previous, guint64 current)
{
    if (!has_previous)
        return RTSP_FRAME_ORDER_FIRST;
    if (current == previous)
        return RTSP_FRAME_ORDER_DUPLICATE;
    if (current < previous)
        return RTSP_FRAME_ORDER_BACKWARD;
    if (previous != G_MAXUINT64 && current == previous + 1)
        return RTSP_FRAME_ORDER_NEXT;
    return RTSP_FRAME_ORDER_GAP;
}

gboolean rtsp_frame_sync_sei_validation_success(
    gboolean expect_sei, guint64 accepted_frames, guint64 missing_sei,
    guint64 invalid_sei, guint64 duplicates, guint64 backwards)
{
    if (duplicates != 0 || backwards != 0 || invalid_sei != 0)
        return FALSE;
    if (expect_sei)
        return accepted_frames > 0 && missing_sei == 0;
    return accepted_frames == 0 && missing_sei > 0;
}

gboolean rtsp_frame_sync_channel_validation_success(
    const RtspFrameSyncChannelValidation *validation)
{
    if (!validation ||
        validation->startup_missing_sei > validation->missing_sei ||
        validation->startup_gap_events > validation->gap_events)
        return FALSE;

    const guint64 allowed_missing = validation->allow_startup_loss
        ? validation->startup_missing_sei : 0;
    const guint64 allowed_gap_events = validation->allow_startup_loss
        ? validation->startup_gap_events : 0;
    if (!rtsp_frame_sync_sei_validation_success(
            validation->expect_sei, validation->accepted_frames,
            validation->missing_sei - allowed_missing,
            validation->invalid_sei, validation->duplicates,
            validation->backwards))
        return FALSE;

    const guint64 operational_gap_events =
        validation->gap_events - allowed_gap_events;
    if (validation->expect_gap)
        return operational_gap_events > 0 &&
               validation->groups_after_gap > 0;
    return operational_gap_events == 0;
}

gboolean rtsp_validation_stop_is_expected(const gchar *stop_reason,
                                           gboolean fatal_bus,
                                           gboolean eos_seen,
                                           gboolean expect_eos)
{
    if (!stop_reason || fatal_bus)
        return FALSE;
    if (expect_eos)
        return eos_seen && g_strcmp0(stop_reason, "eos") == 0;
    return !eos_seen && g_strcmp0(stop_reason, "timeout") == 0;
}

gboolean rtsp_decoder_recovery_observe_gap(
    RtspDecoderRecoveryWindow *window, guint64 decoded_samples,
    gint64 previous_decoded_ns, gint64 decoded_ns,
    gint64 expected_gap_ns)
{
    if (!window || decoded_samples == 0 || previous_decoded_ns < 0 ||
        decoded_ns <= previous_decoded_ns || expected_gap_ns <= 0 ||
        decoded_ns - previous_decoded_ns < expected_gap_ns)
        return FALSE;

    window->gap_observed = TRUE;
    window->decoded_before_gap = TRUE;
    window->gap_previous_decoded_ns = previous_decoded_ns;
    window->observed_gap_ns = decoded_ns - previous_decoded_ns;
    window->recovery_idr_count = 0;
    window->decoded_after_recovery_idr = 0;
    window->recovery_idr_pts = G_MAXUINT64;
    return TRUE;
}

gboolean rtsp_decoder_validation_success(
    const RtspDecoderValidation *validation)
{
    if (!validation || validation->decoded_samples == 0 ||
        validation->idr_count == 0 ||
        !rtsp_validation_stop_is_expected(
            validation->stop_reason, validation->fatal_bus,
            validation->eos_seen, validation->expect_eos))
        return FALSE;

    return !validation->expect_recovery ||
           (validation->decoded_before_gap && validation->gap_observed &&
            validation->expected_gap_ns > 0 &&
            validation->observed_gap_ns >= validation->expected_gap_ns &&
            validation->recovery_idr_count > 0 &&
            validation->decoded_after_recovery_idr > 0);
}
