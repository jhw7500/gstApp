#include <glib.h>
#include <gst/gst.h>

#include "../rtspSync.h"

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

typedef struct
{
    gint destroyed;
} Payload;

static void destroy_payload(gpointer data)
{
    Payload *payload = (Payload *)data;
    payload->destroyed++;
}

typedef struct
{
    RtspSyncGeneration *generation;
    gint64 elapsed_us;
    gboolean completed;
} WaitData;

static gpointer wait_thread(gpointer data)
{
    WaitData *wait = (WaitData *)data;
    gint64 started = g_get_monotonic_time();
    wait->completed = rtsp_sync_generation_wait(
        wait->generation, 5 * G_USEC_PER_SEC, 10 * 1000, NULL, NULL);
    wait->elapsed_us = g_get_monotonic_time() - started;
    return NULL;
}

static gboolean cancel_now(gpointer data)
{
    return data != NULL;
}

static CmdArg make_args(void)
{
    CmdArg arg = {};
    arg.enc = (gchar *)ENC_H265;
    arg.stream_en[STREAM_REC] = TRUE;
    arg.stream_en[STREAM_RTSP] = TRUE;
    arg.fps[STREAM_REC][0] = 30;
    arg.fps[STREAM_RTSP][0] = 15;
    return arg;
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    /* C1/I6: only a bounded H.265 video channel with byte-stream AU caps. */
    CmdArg arg = make_args();
    GstCaps *valid_caps = gst_caps_from_string(
        "video/x-h265,stream-format=byte-stream,alignment=au,framerate=30/1");
    GstCaps *audio_caps = gst_caps_from_string(
        "audio/mpeg,mpegversion=(int)1,layer=(int)3");
    GstCaps *packetized_caps = gst_caps_from_string(
        "video/x-h265,stream-format=hvc1,alignment=au,framerate=30/1");
    GstCaps *nal_caps = gst_caps_from_string(
        "video/x-h265,stream-format=byte-stream,alignment=nal,framerate=30/1");
    GstCaps *incomplete_caps = gst_caps_from_string(
        "video/x-h265,stream-format=byte-stream,alignment=au");

    CHECK(rtsp_frame_id_insertion_allowed(&arg, 0, valid_caps));
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, MAX_CHANNEL, audio_caps));
    CHECK(rtsp_frame_id_rate_for_channel(&arg, MAX_CHANNEL) == 0);
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, 0, audio_caps));
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, 0, packetized_caps));
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, 0, nal_caps));
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, 0, incomplete_caps));
    static const guint8 annex_b_3[] = {
        0x00, 0x00, 0x01, 0x26, 0x01
    };
    static const guint8 annex_b_4[] = {
        0x00, 0x00, 0x00, 0x01, 0x26, 0x01
    };
    static const guint8 packetized[] = {
        0x00, 0x00, 0x00, 0x02, 0x26, 0x01
    };
    GstBuffer *annex_buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_READONLY, (gpointer)annex_b_3,
        sizeof(annex_b_3), 0, sizeof(annex_b_3), NULL, NULL);
    CHECK(rtsp_h265_annex_b_au_buffer(annex_buffer));
    gst_buffer_unref(annex_buffer);
    annex_buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_READONLY, (gpointer)annex_b_4,
        sizeof(annex_b_4), 0, sizeof(annex_b_4), NULL, NULL);
    CHECK(rtsp_h265_annex_b_au_buffer(annex_buffer));
    gst_buffer_unref(annex_buffer);
    annex_buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_READONLY, (gpointer)packetized,
        sizeof(packetized), 0, sizeof(packetized), NULL, NULL);
    CHECK(!rtsp_h265_annex_b_au_buffer(annex_buffer));
    gst_buffer_unref(annex_buffer);
    CHECK(!rtsp_h265_annex_b_au_buffer(NULL));
    guint64 negotiated_frame_id = 0;
    CHECK(rtsp_frame_id_from_caps_pts(valid_caps, GST_SECOND,
                                      &negotiated_frame_id));
    CHECK(negotiated_frame_id == 30);
    negotiated_frame_id = 77;
    CHECK(!rtsp_frame_id_from_caps_pts(incomplete_caps, GST_SECOND,
                                       &negotiated_frame_id));
    CHECK(negotiated_frame_id == 77);
    GstCaps *zero_rate_caps = gst_caps_from_string(
        "video/x-h265,stream-format=byte-stream,alignment=au,framerate=0/1");
    CHECK(!rtsp_frame_id_from_caps_pts(zero_rate_caps, GST_SECOND,
                                       &negotiated_frame_id));
    CHECK(negotiated_frame_id == 77);
    gst_caps_unref(zero_rate_caps);
    arg.enc = (gchar *)ENC_H264;
    CHECK(!rtsp_frame_id_insertion_allowed(&arg, 0, valid_caps));

    /* C1: the common media handler must never advertise video caps to ch4. */
    GstCaps *fallback = rtsp_media_fallback_caps(0, ENC_H265);
    CHECK(fallback != NULL);
    CHECK(gst_caps_can_intersect(fallback, valid_caps));
    CHECK(g_strcmp0(gst_structure_get_name(
                        gst_caps_get_structure(fallback, 0)),
                    "video/x-h265") == 0);
    gst_caps_unref(fallback);
    fallback = rtsp_media_fallback_caps(0, ENC_H264);
    CHECK(fallback != NULL);
    CHECK(g_strcmp0(gst_structure_get_name(
                        gst_caps_get_structure(fallback, 0)),
                    "video/x-h264") == 0);
    gst_caps_unref(fallback);
    fallback = rtsp_media_fallback_caps(MAX_CHANNEL, ENC_H265);
    CHECK(fallback != NULL);
    const GstStructure *fallback_structure =
        gst_caps_get_structure(fallback, 0);
    gint fallback_mpegversion = 0;
    gint fallback_layer = 0;
    CHECK(g_strcmp0(gst_structure_get_name(fallback_structure),
                    "audio/mpeg") == 0);
    CHECK(gst_structure_get_int(fallback_structure, "mpegversion",
                                &fallback_mpegversion));
    CHECK(fallback_mpegversion == 1);
    CHECK(gst_structure_get_int(fallback_structure, "layer",
                                &fallback_layer));
    CHECK(fallback_layer == 3);
    gst_caps_unref(fallback);
    CHECK(rtsp_media_fallback_caps(MAX_CHANNEL + 1, ENC_H265) == NULL);
    CHECK(rtsp_media_fallback_caps(0, "unsupported") == NULL);

    gst_caps_unref(incomplete_caps);
    gst_caps_unref(nal_caps);
    gst_caps_unref(packetized_caps);
    gst_caps_unref(audio_caps);
    gst_caps_unref(valid_caps);

    /* C2: single encoder follows the record encoder's real configured rate. */
    arg = make_args();
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 30);
    guint64 frame_id = 0;
    CHECK(rtsp_frame_id_from_pts(GST_SECOND, 30, &frame_id));
    CHECK(frame_id == 30);
    CHECK(rtsp_frame_id_from_pts(GST_SECOND + GST_SECOND / 30,
                                 rtsp_frame_id_rate_for_channel(&arg, 0),
                                 &frame_id));
    CHECK(frame_id == 31);

    arg.stream_en[STREAM_REC] = FALSE;
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 15);
    arg.stream_en[STREAM_REC] = TRUE;
    arg.fps[STREAM_REC][0] = 15;
    arg.fps[STREAM_RTSP][0] = 30;
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 15);
    arg.dual_enc = TRUE;
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 30);
    arg.fps[STREAM_RTSP][0] = 0;
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 0);
    frame_id = 77;
    CHECK(!rtsp_frame_id_from_pts(GST_SECOND, 0, &frame_id));
    CHECK(frame_id == 77);
    arg.fps[STREAM_RTSP][0] = -1;
    CHECK(rtsp_frame_id_rate_for_channel(&arg, 0) == 0);
    CHECK(!rtsp_frame_id_from_pts(GST_CLOCK_TIME_NONE, 15, &frame_id));
    CHECK(frame_id == 77);

    GstCaps *fraction_caps = gst_caps_from_string(
        "video/x-h265,stream-format=byte-stream,alignment=au,framerate=30000/1001");
    guint64 fraction_id = 77;
    CHECK(rtsp_frame_id_from_caps_pts(fraction_caps,
                                      1001 * GST_SECOND / 30000,
                                      &fraction_id));
    CHECK(fraction_id == 1);
    fraction_id = 77;
    CHECK(!rtsp_frame_id_from_caps_pts(NULL, GST_SECOND, &fraction_id));
    CHECK(fraction_id == 77);
    CHECK(!rtsp_frame_id_from_caps_pts(fraction_caps, GST_CLOCK_TIME_NONE,
                                       &fraction_id));
    CHECK(fraction_id == 77);
    gst_caps_unref(fraction_caps);

    /* I3: a callback ref keeps generation data alive after owner teardown. */
    Payload payload = {};
    RtspSyncGeneration *owner =
        rtsp_sync_generation_new(7, &payload, destroy_payload);
    CHECK(owner != NULL);
    CHECK(rtsp_sync_generation_id(owner) == 7);
    CHECK(rtsp_sync_generation_data(owner) == &payload);
    rtsp_sync_generation_activate(owner);
    CHECK(rtsp_sync_generation_is_active(owner));
    RtspSyncGeneration *callback = rtsp_sync_generation_ref(owner);
    rtsp_sync_generation_deactivate(owner);
    rtsp_sync_generation_unref(owner);
    CHECK(payload.destroyed == 0);
    CHECK(!rtsp_sync_generation_is_active(callback));
    rtsp_sync_generation_unref(callback);
    CHECK(payload.destroyed == 1);

    /* I4: cancellation wakes a nominal five-second wait in bounded time. */
    RtspSyncGeneration *stall =
        rtsp_sync_generation_new(8, NULL, NULL);
    rtsp_sync_generation_activate(stall);
    WaitData wait = { rtsp_sync_generation_ref(stall), 0, FALSE };
    GThread *thread = g_thread_new("stall-wait", wait_thread, &wait);
    g_usleep(20 * 1000);
    rtsp_sync_generation_deactivate(stall);
    g_thread_join(thread);
    rtsp_sync_generation_unref(wait.generation);
    rtsp_sync_generation_unref(stall);
    CHECK(!wait.completed);
    CHECK(wait.elapsed_us < 500 * 1000);

    RtspSyncGeneration *external =
        rtsp_sync_generation_new(9, NULL, NULL);
    rtsp_sync_generation_activate(external);
    gint64 external_started = g_get_monotonic_time();
    CHECK(!rtsp_sync_generation_wait(external, 5 * G_USEC_PER_SEC,
                                     10 * 1000, cancel_now, external));
    CHECK(g_get_monotonic_time() - external_started < 100 * 1000);
    CHECK(rtsp_sync_generation_is_active(external));
    rtsp_sync_generation_deactivate(external);
    rtsp_sync_generation_unref(external);

    g_print("rtsp sync core test: %d checks, %d failures -> %s\n",
            checks, failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
