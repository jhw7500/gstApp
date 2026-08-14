#include "rtspSync.h"

struct _RtspSyncGeneration
{
    volatile gint ref_count;
    guint64 id;
    volatile gint active;
    volatile gint cancelled;
    gpointer data;
    GDestroyNotify destroy_data;
};

RtspSyncGeneration *rtsp_sync_generation_new(guint64 id, gpointer data,
                                              GDestroyNotify destroy_data)
{
    RtspSyncGeneration *generation = g_new0(RtspSyncGeneration, 1);
    if (!generation)
        return NULL;

    generation->ref_count = 1;
    generation->id = id;
    generation->data = data;
    generation->destroy_data = destroy_data;
    return generation;
}

RtspSyncGeneration *rtsp_sync_generation_ref(
    RtspSyncGeneration *generation)
{
    if (generation)
        g_atomic_int_inc(&generation->ref_count);
    return generation;
}

void rtsp_sync_generation_unref(RtspSyncGeneration *generation)
{
    if (!generation ||
        !g_atomic_int_dec_and_test(&generation->ref_count))
        return;

    if (generation->destroy_data)
        generation->destroy_data(generation->data);
    g_free(generation);
}

void rtsp_sync_generation_activate(RtspSyncGeneration *generation)
{
    if (!generation || g_atomic_int_get(&generation->cancelled))
        return;
    g_atomic_int_set(&generation->active, 1);
}

void rtsp_sync_generation_deactivate(RtspSyncGeneration *generation)
{
    if (!generation)
        return;
    g_atomic_int_set(&generation->cancelled, 1);
    g_atomic_int_set(&generation->active, 0);
}

gboolean rtsp_sync_generation_is_active(
    const RtspSyncGeneration *generation)
{
    if (!generation)
        return FALSE;
    return g_atomic_int_get(&generation->active) != 0 &&
           g_atomic_int_get(&generation->cancelled) == 0;
}

guint64 rtsp_sync_generation_id(const RtspSyncGeneration *generation)
{
    return generation ? generation->id : 0;
}

gpointer rtsp_sync_generation_data(const RtspSyncGeneration *generation)
{
    return generation ? generation->data : NULL;
}

gboolean rtsp_sync_generation_wait(RtspSyncGeneration *generation,
                                   gint64 duration_us, gint64 slice_us,
                                   RtspSyncCancelFunc cancelled,
                                   gpointer user_data)
{
    if (!generation || duration_us < 0 || slice_us <= 0)
        return FALSE;

    const gint64 started = g_get_monotonic_time();
    const gint64 deadline =
        duration_us > G_MAXINT64 - started ? G_MAXINT64
                                          : started + duration_us;

    while (TRUE)
    {
        if (!rtsp_sync_generation_is_active(generation) ||
            (cancelled && cancelled(user_data)))
            return FALSE;

        const gint64 now = g_get_monotonic_time();
        if (now >= deadline)
            return TRUE;

        const gint64 remaining = deadline - now;
        g_usleep((gulong)MIN(remaining, slice_us));
    }
}

guint rtsp_frame_id_rate_for_channel(const CmdArg *arg, guint channel)
{
    if (!arg || channel >= MAX_CHANNEL || !arg->stream_en[STREAM_RTSP])
        return 0;

    gint fps;
    if (arg->dual_enc)
    {
        fps = arg->fps[STREAM_RTSP][channel];
    }
    else if (arg->stream_en[STREAM_REC])
    {
        fps = arg->fps[STREAM_REC][channel];
    }
    else
    {
        fps = arg->fps[STREAM_RTSP][channel];
    }

    return fps > 0 ? (guint)fps : 0;
}

gboolean rtsp_frame_id_from_pts(GstClockTime origin_pts, guint fps,
                                guint64 *frame_id)
{
    if (!frame_id || fps == 0 || !GST_CLOCK_TIME_IS_VALID(origin_pts))
        return FALSE;

    *frame_id = gst_util_uint64_scale_round(origin_pts, fps, GST_SECOND);
    return TRUE;
}

gboolean rtsp_frame_id_from_caps_pts(const GstCaps *caps,
                                     GstClockTime origin_pts,
                                     guint64 *frame_id)
{
    if (!caps || !frame_id || !GST_CLOCK_TIME_IS_VALID(origin_pts) ||
        !gst_caps_is_fixed(caps) || gst_caps_get_size(caps) != 1)
        return FALSE;

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint numerator = 0;
    gint denominator = 0;
    if (!structure ||
        !gst_structure_get_fraction(structure, "framerate", &numerator,
                                    &denominator) ||
        numerator <= 0 || denominator <= 0)
        return FALSE;

    *frame_id = gst_util_uint64_scale_round(
        origin_pts, (guint64)numerator,
        (guint64)GST_SECOND * (guint64)denominator);
    return TRUE;
}

gboolean rtsp_h265_au_caps_compatible(const CmdArg *arg, guint channel,
                                      const GstCaps *caps)
{
    if (!arg || channel >= MAX_CHANNEL || !caps ||
        g_strcmp0(arg->enc, ENC_H265) != 0 ||
        !gst_caps_is_fixed(caps) || gst_caps_get_size(caps) != 1)
        return FALSE;

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    if (!structure ||
        g_strcmp0(gst_structure_get_name(structure), "video/x-h265") != 0)
        return FALSE;

    gint numerator = 0;
    gint denominator = 0;
    return g_strcmp0(gst_structure_get_string(structure, "stream-format"),
                     "byte-stream") == 0 &&
           g_strcmp0(gst_structure_get_string(structure, "alignment"),
                     "au") == 0 &&
           gst_structure_get_fraction(structure, "framerate", &numerator,
                                      &denominator) &&
           numerator > 0 && denominator > 0;
}

gboolean rtsp_h265_annex_b_au_buffer(GstBuffer *buffer)
{
    if (!buffer || gst_buffer_get_size(buffer) < 5)
        return FALSE;

    guint8 prefix[4] = { 0 };
    const gsize extracted = gst_buffer_extract(
        buffer, 0, prefix, sizeof(prefix));
    return extracted >= 3 && prefix[0] == 0 && prefix[1] == 0 &&
           (prefix[2] == 1 ||
            (extracted == 4 && prefix[2] == 0 && prefix[3] == 1));
}

gboolean rtsp_frame_id_insertion_allowed(const CmdArg *arg, guint channel,
                                         const GstCaps *caps)
{
    return rtsp_h265_au_caps_compatible(arg, channel, caps) &&
           rtsp_frame_id_rate_for_channel(arg, channel) > 0;
}

GstCaps *rtsp_media_fallback_caps(guint channel, const gchar *encoder)
{
    if (channel == MAX_CHANNEL)
    {
        return gst_caps_new_simple("audio/mpeg",
                                   "mpegversion", G_TYPE_INT, 1,
                                   "layer", G_TYPE_INT, 3,
                                   NULL);
    }

    if (channel >= MAX_CHANNEL)
        return NULL;

    if (g_strcmp0(encoder, ENC_H265) == 0)
    {
        return gst_caps_new_simple("video/x-h265",
                                   "stream-format", G_TYPE_STRING,
                                   "byte-stream",
                                   "alignment", G_TYPE_STRING, "au",
                                   NULL);
    }

    if (g_strcmp0(encoder, ENC_H264) == 0)
    {
        return gst_caps_new_simple("video/x-h264",
                                   "stream-format", G_TYPE_STRING,
                                   "byte-stream",
                                   "alignment", G_TYPE_STRING, "au",
                                   NULL);
    }

    return NULL;
}
