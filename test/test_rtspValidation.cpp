#include <glib.h>
#include <gst/gst.h>
#include <string.h>

#include "rtspValidation.h"

static gint failures = 0;
static gint checks = 0;

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        checks++;                                                       \
        if (!(condition))                                               \
        {                                                               \
            g_printerr("FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                       #condition);                                     \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void append_escaped(GByteArray *array, const guint8 *data, gsize size)
{
    guint zero_count = 0;
    for (gsize i = 0; i < size; i++)
    {
        if (zero_count >= 2 && data[i] <= 0x03)
        {
            const guint8 prevention = 0x03;
            g_byte_array_append(array, &prevention, 1);
            zero_count = 0;
        }
        g_byte_array_append(array, data + i, 1);
        zero_count = data[i] == 0 ? zero_count + 1 : 0;
    }
}

static GByteArray *make_au(gboolean four_byte_start, guint nal_type,
                           guint payload_type, guint8 country,
                           guint8 extension, const guint8 magic[8],
                           guint8 version, guint8 channel,
                           guint declared_size_delta, gboolean trailing_bits)
{
    GByteArray *array = g_byte_array_new();
    static const guint8 start4[] = { 0x00, 0x00, 0x00, 0x01 };
    static const guint8 start3[] = { 0x00, 0x00, 0x01 };
    g_byte_array_append(array, four_byte_start ? start4 : start3,
                        four_byte_start ? sizeof(start4) : sizeof(start3));
    guint8 header[2] = { (guint8)(nal_type << 1), 0x01 };
    g_byte_array_append(array, header, sizeof(header));

    guint8 payload[30] = { 0 };
    payload[0] = country;
    payload[1] = extension;
    memcpy(payload + 2, magic, 8);
    payload[10] = version;
    payload[11] = channel;
    GST_WRITE_UINT64_BE(payload + 14, G_GUINT64_CONSTANT(0x0102030405060708));
    GST_WRITE_UINT64_BE(payload + 22, G_GUINT64_CONSTANT(0x000000003b9aca00));

    GByteArray *rbsp = g_byte_array_new();
    guint8 type = (guint8)payload_type;
    guint8 size = (guint8)(sizeof(payload) + declared_size_delta);
    g_byte_array_append(rbsp, &type, 1);
    g_byte_array_append(rbsp, &size, 1);
    g_byte_array_append(rbsp, payload, sizeof(payload));
    if (trailing_bits)
    {
        const guint8 trailing = 0x80;
        g_byte_array_append(rbsp, &trailing, 1);
    }
    append_escaped(array, rbsp->data, rbsp->len);
    g_byte_array_unref(rbsp);
    /* A Frame ID is meaningful only on a video access unit. */
    g_byte_array_append(array, start3, sizeof(start3));
    const guint8 vcl[] = { (guint8)(1 << 1), 0x01, 0xaa };
    g_byte_array_append(array, vcl, sizeof(vcl));
    return array;
}

static void expect_valid(GByteArray *au, guint8 expected_channel)
{
    RtspFrameIdRecord record = {};
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, expected_channel,
                                 &record) == RTSP_FRAME_ID_PARSE_OK);
    CHECK(record.version == 1);
    CHECK(record.channel == expected_channel);
    CHECK(record.frame_id == G_GUINT64_CONSTANT(0x0102030405060708));
    CHECK(record.origin_pts == G_GUINT64_CONSTANT(0x000000003b9aca00));
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    static const guint8 magic[8] = {
        'G', 'S', 'T', 'S', 'Y', 'N', 'C', '1'
    };

    GByteArray *au = make_au(TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    expect_valid(au, 2);
    g_byte_array_unref(au);

    au = make_au(FALSE, 40, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    expect_valid(au, 2);
    g_byte_array_unref(au);

    /* Literal magic in a slice is not an SEI message. */
    const guint8 slice_magic[] = {
        0x00, 0x00, 0x01, 0x02, 0x01,
        'G', 'S', 'T', 'S', 'Y', 'N', 'C', '1',
        0x01, 0x02, 0x00, 0x00
    };
    RtspFrameIdRecord record = {};
    CHECK(rtsp_frame_id_parse_au(slice_magic, sizeof(slice_magic), 2,
                                 &record) == RTSP_FRAME_ID_PARSE_NOT_FOUND);

    au = make_au(TRUE, 38, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_NOT_FOUND);

    const guint8 parameter_set_only[] = {
        0x00, 0x00, 0x01, (guint8)(32 << 1), 0x01, 0xaa
    };
    CHECK(rtsp_frame_id_parse_au(parameter_set_only,
                                 sizeof(parameter_set_only), 2,
                                 &record) == RTSP_FRAME_ID_PARSE_NOT_FRAME);

    const guint8 no_start[] = { 0x02, 0x01, 0xaa, 0xbb, 0xcc };
    CHECK(rtsp_frame_id_parse_au(no_start, sizeof(no_start), 2, &record) ==
          RTSP_FRAME_ID_PARSE_MALFORMED);
    CHECK(rtsp_frame_sync_sei_validation_success(FALSE, 0, 10, 0, 0, 0));
    CHECK(!rtsp_frame_sync_sei_validation_success(FALSE, 0, 10, 1, 0, 0));
    CHECK(!rtsp_frame_sync_sei_validation_success(FALSE, 1, 10, 0, 0, 0));
    CHECK(rtsp_frame_sync_sei_validation_success(TRUE, 10, 0, 0, 0, 0));
    CHECK(!rtsp_frame_sync_sei_validation_success(TRUE, 10, 0, 0, 1, 0));
    CHECK(!rtsp_frame_sync_sei_validation_success(TRUE, 10, 0, 0, 0, 1));

    RtspFrameSyncChannelValidation channel_validation = {};
    channel_validation.expect_sei = TRUE;
    channel_validation.accepted_frames = 10;
    channel_validation.missing_sei = 1;
    channel_validation.gap_events = 1;
    channel_validation.startup_missing_sei = 1;
    channel_validation.startup_gap_events = 1;
    CHECK(!rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.allow_startup_loss = TRUE;
    CHECK(rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.missing_sei = 2;
    CHECK(!rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.missing_sei = 1;
    channel_validation.gap_events = 2;
    CHECK(!rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.expect_gap = TRUE;
    channel_validation.groups_after_gap = 4;
    CHECK(rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.gap_events = 1;
    CHECK(!rtsp_frame_sync_channel_validation_success(
        &channel_validation));
    channel_validation.startup_gap_events = 2;
    CHECK(!rtsp_frame_sync_channel_validation_success(
        &channel_validation));

    GByteArray *malformed_vcl_then_valid = g_byte_array_new();
    static const guint8 malformed_vcl[] = {
        0x00, 0x00, 0x01, 0x02, 0x00, 0xaa
    };
    g_byte_array_append(malformed_vcl_then_valid, malformed_vcl,
                        sizeof(malformed_vcl));
    GByteArray *valid_after_malformed = make_au(
        TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    g_byte_array_append(malformed_vcl_then_valid,
                        valid_after_malformed->data,
                        valid_after_malformed->len);
    CHECK(rtsp_frame_id_parse_au(malformed_vcl_then_valid->data,
                                 malformed_vcl_then_valid->len, 2,
                                 &record) == RTSP_FRAME_ID_PARSE_MALFORMED);
    g_byte_array_unref(valid_after_malformed);
    g_byte_array_unref(malformed_vcl_then_valid);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 5, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_NOT_FOUND);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xb5, 0xc1, magic, 1, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_IDENTITY);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xff, 0xb5, magic, 1, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_IDENTITY);
    g_byte_array_unref(au);

    guint8 wrong_magic[8] = { 'B', 'A', 'D', 'S', 'Y', 'N', 'C', '1' };
    au = make_au(TRUE, 39, 4, 0xff, 0xc1, wrong_magic, 1, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_IDENTITY);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xff, 0xc1, magic, 2, 2, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_VERSION);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xff, 0xc1, magic, 1, 3, 0, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_CHANNEL);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 1, TRUE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_MALFORMED);
    g_byte_array_unref(au);

    au = make_au(TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 0, FALSE);
    CHECK(rtsp_frame_id_parse_au(au->data, au->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_MALFORMED);
    g_byte_array_unref(au);

    const guint8 bad_epb[] = {
        0x00, 0x00, 0x01, 0x4e, 0x01, 0x04, 0x1e,
        0x00, 0x00, 0x03, 0x04, 0x80
    };
    CHECK(rtsp_frame_id_parse_au(bad_epb, sizeof(bad_epb), 2, &record) ==
          RTSP_FRAME_ID_PARSE_MALFORMED);

    const guint8 truncated_sei_header[] = {
        0x00, 0x00, 0x01, 0x4e
    };
    CHECK(rtsp_frame_id_parse_au(truncated_sei_header,
                                 sizeof(truncated_sei_header), 2,
                                 &record) == RTSP_FRAME_ID_PARSE_MALFORMED);

    const guint8 short_registered_payload[] = {
        0x00, 0x00, 0x01, 0x4e, 0x01, 0x04, 0x01, 0xff, 0x80
    };
    CHECK(rtsp_frame_id_parse_au(short_registered_payload,
                                 sizeof(short_registered_payload), 2,
                                 &record) == RTSP_FRAME_ID_PARSE_MALFORMED);

    GByteArray *duplicate = make_au(
        TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    GByteArray *second = make_au(
        FALSE, 40, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    g_byte_array_append(duplicate, second->data, second->len);
    record.frame_id = 99;
    CHECK(rtsp_frame_id_parse_au(duplicate->data, duplicate->len, 2,
                                 &record) == RTSP_FRAME_ID_PARSE_MALFORMED);
    CHECK(record.frame_id == 99);
    g_byte_array_unref(second);
    g_byte_array_unref(duplicate);

    GByteArray *wrong_then_valid = make_au(
        TRUE, 39, 4, 0xff, 0xc1, wrong_magic, 1, 2, 0, TRUE);
    second = make_au(
        FALSE, 40, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    g_byte_array_append(wrong_then_valid, second->data, second->len);
    record.frame_id = 101;
    CHECK(rtsp_frame_id_parse_au(wrong_then_valid->data,
                                 wrong_then_valid->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_IDENTITY);
    CHECK(record.frame_id == 101);
    g_byte_array_unref(second);
    g_byte_array_unref(wrong_then_valid);

    GByteArray *valid_then_wrong = make_au(
        TRUE, 39, 4, 0xff, 0xc1, magic, 1, 2, 0, TRUE);
    second = make_au(
        FALSE, 40, 4, 0xff, 0xc1, wrong_magic, 1, 2, 0, TRUE);
    g_byte_array_append(valid_then_wrong, second->data, second->len);
    record.frame_id = 102;
    CHECK(rtsp_frame_id_parse_au(valid_then_wrong->data,
                                 valid_then_wrong->len, 2, &record) ==
          RTSP_FRAME_ID_PARSE_WRONG_IDENTITY);
    CHECK(record.frame_id == 102);
    g_byte_array_unref(second);
    g_byte_array_unref(valid_then_wrong);

    gboolean contains_idr = FALSE;
    const guint8 idr4[] = {
        0x00, 0x00, 0x00, 0x01, (guint8)(19 << 1), 0x01, 0xaa
    };
    CHECK(rtsp_h265_au_contains_idr(idr4, sizeof(idr4), &contains_idr));
    CHECK(contains_idr);

    const guint8 idr_after_slice[] = {
        0x00, 0x00, 0x01, (guint8)(1 << 1), 0x01, 0xaa,
        0x00, 0x00, 0x01, (guint8)(20 << 1), 0x01, 0xbb
    };
    contains_idr = FALSE;
    CHECK(rtsp_h265_au_contains_idr(idr_after_slice,
                                    sizeof(idr_after_slice),
                                    &contains_idr));
    CHECK(contains_idr);

    const guint8 non_idr[] = {
        0x00, 0x00, 0x01, (guint8)(1 << 1), 0x01, 0xaa
    };
    contains_idr = TRUE;
    CHECK(rtsp_h265_au_contains_idr(non_idr, sizeof(non_idr),
                                    &contains_idr));
    CHECK(!contains_idr);

    const guint8 no_start_code[] = { 0x26, 0x01, 0xaa };
    contains_idr = TRUE;
    CHECK(!rtsp_h265_au_contains_idr(no_start_code,
                                     sizeof(no_start_code),
                                     &contains_idr));
    CHECK(contains_idr);
    const guint8 leading_garbage[] = {
        0x7f, 0x00, 0x00, 0x01, (guint8)(19 << 1), 0x01
    };
    contains_idr = FALSE;
    CHECK(!rtsp_h265_au_contains_idr(leading_garbage,
                                     sizeof(leading_garbage),
                                     &contains_idr));
    CHECK(!contains_idr);
    const guint8 truncated_nal[] = { 0x00, 0x00, 0x01, 0x26 };
    CHECK(!rtsp_h265_au_contains_idr(truncated_nal,
                                     sizeof(truncated_nal),
                                     &contains_idr));
    const guint8 forbidden_bit[] = {
        0x00, 0x00, 0x01, (guint8)(0x80 | (19 << 1)), 0x01
    };
    CHECK(!rtsp_h265_au_contains_idr(forbidden_bit,
                                     sizeof(forbidden_bit),
                                     &contains_idr));
    const guint8 invalid_temporal_id[] = {
        0x00, 0x00, 0x01, (guint8)(19 << 1), 0x00
    };
    CHECK(!rtsp_h265_au_contains_idr(invalid_temporal_id,
                                     sizeof(invalid_temporal_id),
                                     &contains_idr));

    CHECK(rtsp_frame_order_check(FALSE, 0, 10) == RTSP_FRAME_ORDER_FIRST);
    CHECK(rtsp_frame_order_check(TRUE, 10, 11) == RTSP_FRAME_ORDER_NEXT);
    CHECK(rtsp_frame_order_check(TRUE, 10, 13) == RTSP_FRAME_ORDER_GAP);
    CHECK(rtsp_frame_order_check(TRUE, 10, 10) == RTSP_FRAME_ORDER_DUPLICATE);
    CHECK(rtsp_frame_order_check(TRUE, 10, 9) == RTSP_FRAME_ORDER_BACKWARD);
    CHECK(rtsp_frame_order_check(TRUE, G_MAXUINT64, 0) ==
          RTSP_FRAME_ORDER_BACKWARD);

    CHECK(rtsp_validation_stop_is_expected("timeout", FALSE, FALSE, FALSE));
    CHECK(!rtsp_validation_stop_is_expected("error", TRUE, FALSE, FALSE));
    CHECK(!rtsp_validation_stop_is_expected("eos", FALSE, TRUE, FALSE));
    CHECK(rtsp_validation_stop_is_expected("eos", FALSE, TRUE, TRUE));

    RtspDecoderValidation decoder = {};
    decoder.decoded_samples = 20;
    decoder.idr_count = 1;
    decoder.stop_reason = "timeout";
    CHECK(rtsp_decoder_validation_success(&decoder));
    decoder.fatal_bus = TRUE;
    CHECK(!rtsp_decoder_validation_success(&decoder));
    decoder.fatal_bus = FALSE;
    decoder.expect_recovery = TRUE;
    CHECK(!rtsp_decoder_validation_success(&decoder));
    decoder.recovery_idr_count = 1;
    decoder.decoded_after_recovery_idr = 1;
    CHECK(!rtsp_decoder_validation_success(&decoder));
    decoder.decoded_before_gap = TRUE;
    decoder.gap_observed = TRUE;
    decoder.expected_gap_ns = GST_SECOND;
    decoder.observed_gap_ns = GST_SECOND - 1;
    CHECK(!rtsp_decoder_validation_success(&decoder));
    decoder.observed_gap_ns = GST_SECOND;
    CHECK(rtsp_decoder_validation_success(&decoder));

    RtspDecoderRecoveryWindow window = {};
    window.recovery_idr_count = 3;
    window.decoded_after_recovery_idr = 1;
    window.recovery_idr_pts = 99;
    CHECK(!rtsp_decoder_recovery_observe_gap(
        &window, 10, GST_SECOND, GST_SECOND + GST_MSECOND,
        2 * GST_MSECOND));
    CHECK(window.recovery_idr_count == 3);
    CHECK(window.decoded_after_recovery_idr == 1);
    CHECK(window.recovery_idr_pts == 99);

    CHECK(rtsp_decoder_recovery_observe_gap(
        &window, 10, GST_SECOND, GST_SECOND + 3 * GST_MSECOND,
        2 * GST_MSECOND));
    CHECK(window.gap_observed);
    CHECK(window.decoded_before_gap);
    CHECK(window.gap_previous_decoded_ns == (gint64)GST_SECOND);
    CHECK(window.observed_gap_ns == 3 * (gint64)GST_MSECOND);
    CHECK(window.recovery_idr_count == 0);
    CHECK(window.decoded_after_recovery_idr == 0);
    CHECK(window.recovery_idr_pts == G_MAXUINT64);

    window.recovery_idr_count = 2;
    window.decoded_after_recovery_idr = 1;
    window.recovery_idr_pts = 123;
    CHECK(rtsp_decoder_recovery_observe_gap(
        &window, 20, 2 * GST_SECOND,
        2 * GST_SECOND + 4 * GST_MSECOND,
        2 * GST_MSECOND));
    CHECK(window.gap_previous_decoded_ns == 2 * (gint64)GST_SECOND);
    CHECK(window.observed_gap_ns == 4 * (gint64)GST_MSECOND);
    CHECK(window.recovery_idr_count == 0);
    CHECK(window.decoded_after_recovery_idr == 0);
    CHECK(window.recovery_idr_pts == G_MAXUINT64);

    g_print("rtsp validation test: %d checks, %d failures -> %s\n",
            checks, failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
