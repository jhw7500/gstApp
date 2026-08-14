#include "rtspValidation.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <glib.h>

#include <stdio.h>

typedef struct
{
    GMutex lock;
    GMainLoop *loop;
    guint timeout_source;
    const gchar *stop_reason;
    gboolean fatal_bus;
    gboolean eos_seen;

    guint64 encoded_samples;
    guint64 decoded_samples;
    guint64 idr_count;

    GstClockTime first_pts;
    GstClockTime last_pts;
    gint64 first_ns;
    gint64 last_ns;
    gint64 maximum_gap_ns;

    gboolean expect_recovery;
    gint64 expected_gap_ns;
    RtspDecoderRecoveryWindow recovery;

    GstClockTime latest_idr_pts;
    gint64 latest_idr_ns;
} State;

static gint64 monotonic_time_ns(void)
{
    return g_get_monotonic_time() * 1000;
}

static void request_stop(State *state, const gchar *reason,
                         gboolean fatal, gboolean eos)
{
    g_mutex_lock(&state->lock);
    if (fatal)
        state->fatal_bus = TRUE;
    if (eos)
        state->eos_seen = TRUE;
    if (!state->stop_reason || fatal)
        state->stop_reason = reason;
    g_mutex_unlock(&state->lock);

    g_main_loop_quit(state->loop);
}

static gboolean on_timeout(gpointer data)
{
    State *state = (State *)data;

    g_mutex_lock(&state->lock);
    state->timeout_source = 0;
    if (!state->stop_reason)
        state->stop_reason = "timeout";
    g_mutex_unlock(&state->lock);

    g_main_loop_quit(state->loop);
    return G_SOURCE_REMOVE;
}

static gboolean on_bus(GstBus *bus, GstMessage *message, gpointer data)
{
    (void)bus;
    State *state = (State *)data;

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        g_printerr("DECODER_ERROR message=%s debug=%s\n",
                   error ? error->message : "unknown",
                   debug ? debug : "none");
        if (error)
            g_error_free(error);
        g_free(debug);
        request_stop(state, "error", TRUE, FALSE);
        break;
    }
    case GST_MESSAGE_EOS:
        g_printerr("DECODER_EOS\n");
        request_stop(state, "eos", FALSE, TRUE);
        break;
    case GST_MESSAGE_WARNING:
    {
        GError *warning = NULL;
        gchar *debug = NULL;
        gst_message_parse_warning(message, &warning, &debug);
        g_printerr("DECODER_WARNING message=%s debug=%s\n",
                   warning ? warning->message : "unknown",
                   debug ? debug : "none");
        if (warning)
            g_error_free(warning);
        g_free(debug);
        break;
    }
    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}

static gboolean decoded_pts_at_or_after(GstClockTime decoded_pts,
                                        GstClockTime idr_pts)
{
    return GST_CLOCK_TIME_IS_VALID(decoded_pts) &&
           GST_CLOCK_TIME_IS_VALID(idr_pts) && decoded_pts >= idr_pts;
}

static gboolean idr_is_after_gap_boundary(const State *state,
                                          GstClockTime idr_pts,
                                          gint64 idr_ns)
{
    if (!state->recovery.gap_observed ||
        idr_ns <= state->recovery.gap_previous_decoded_ns ||
        idr_ns - state->recovery.gap_previous_decoded_ns <
            state->expected_gap_ns ||
        !GST_CLOCK_TIME_IS_VALID(idr_pts))
        return FALSE;

    /* Arrival order is authoritative across RTSP segment/PTS resets. The
     * subsequent decoded sample is still required to be at/after this IDR's
     * PTS within the new segment. */
    return TRUE;
}

static void observe_recovery_idr_locked(State *state, GstClockTime idr_pts,
                                        gint64 idr_ns)
{
    if (!idr_is_after_gap_boundary(state, idr_pts, idr_ns))
        return;

    state->recovery.recovery_idr_count++;
    state->recovery.recovery_idr_pts = idr_pts;
}

static GstFlowReturn on_encoded(GstAppSink *sink, gpointer data)
{
    State *state = (State *)data;
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
    {
        g_printerr("DECODER_ERROR encoded sample is NULL\n");
        request_stop(state, "encoded-sample-error", TRUE, FALSE);
        return GST_FLOW_ERROR;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map = GST_MAP_INFO_INIT;
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        g_printerr("DECODER_ERROR encoded buffer is unavailable\n");
        gst_sample_unref(sample);
        request_stop(state, "encoded-sample-error", TRUE, FALSE);
        return GST_FLOW_ERROR;
    }

    gboolean contains_idr = FALSE;
    const gboolean valid_au = rtsp_h265_au_contains_idr(
        map.data, map.size, &contains_idr);
    gst_buffer_unmap(buffer, &map);
    if (!valid_au)
    {
        g_printerr("DECODER_ERROR malformed H265 Annex-B access unit\n");
        gst_sample_unref(sample);
        request_stop(state, "encoded-au-error", TRUE, FALSE);
        return GST_FLOW_ERROR;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    const gint64 now_ns = monotonic_time_ns();
    guint64 idr_number = 0;

    g_mutex_lock(&state->lock);
    state->encoded_samples++;
    if (contains_idr)
    {
        state->idr_count++;
        idr_number = state->idr_count;
        state->latest_idr_pts = pts;
        state->latest_idr_ns = now_ns;
        observe_recovery_idr_locked(state, pts, now_ns);
    }
    g_mutex_unlock(&state->lock);

    if (contains_idr)
    {
        g_print("DECODER_IDR count=%" G_GUINT64_FORMAT
                " pts=%" G_GUINT64_FORMAT " mono_ns=%" G_GINT64_FORMAT
                "\n",
                idr_number, pts, now_ns);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static GstFlowReturn on_decoded(GstAppSink *sink, gpointer data)
{
    State *state = (State *)data;
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
    {
        g_printerr("DECODER_ERROR decoded sample is NULL\n");
        request_stop(state, "decoded-sample-error", TRUE, FALSE);
        return GST_FLOW_ERROR;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer)
    {
        g_printerr("DECODER_ERROR decoded buffer is NULL\n");
        gst_sample_unref(sample);
        request_stop(state, "decoded-sample-error", TRUE, FALSE);
        return GST_FLOW_ERROR;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    const gint64 now_ns = monotonic_time_ns();
    gboolean first_sample = FALSE;
    gboolean qualifying_gap = FALSE;
    gint64 observed_gap_ns = 0;

    g_mutex_lock(&state->lock);
    if (state->decoded_samples == 0)
    {
        first_sample = TRUE;
        state->first_pts = pts;
        state->first_ns = now_ns;
    }
    else
    {
        const gint64 gap_ns = now_ns - state->last_ns;
        if (gap_ns > state->maximum_gap_ns)
            state->maximum_gap_ns = gap_ns;
        if (state->expect_recovery)
        {
            qualifying_gap = rtsp_decoder_recovery_observe_gap(
                &state->recovery, state->decoded_samples,
                state->last_ns, now_ns, state->expected_gap_ns);
            if (qualifying_gap)
                observed_gap_ns = gap_ns;
        }
    }

    state->last_pts = pts;
    state->last_ns = now_ns;
    state->decoded_samples++;

    if (qualifying_gap && state->latest_idr_ns > 0)
    {
        observe_recovery_idr_locked(state, state->latest_idr_pts,
                                    state->latest_idr_ns);
    }
    if (state->recovery.recovery_idr_count > 0 &&
        decoded_pts_at_or_after(pts, state->recovery.recovery_idr_pts))
        state->recovery.decoded_after_recovery_idr = 1;
    g_mutex_unlock(&state->lock);

    if (first_sample)
    {
        g_print("DECODER_FIRST pts=%" G_GUINT64_FORMAT
                " mono_ns=%" G_GINT64_FORMAT "\n",
                pts, now_ns);
    }
    if (qualifying_gap)
    {
        g_print("DECODER_GAP observed_ms=%.3f threshold_ms=%.3f\n",
                (gdouble)observed_gap_ns / GST_MSECOND,
                (gdouble)state->expected_gap_ns / GST_MSECOND);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static gboolean parse_positive_int(const gchar *text, gint *value)
{
    if (!text || !*text || !value)
        return FALSE;

    gchar *end = NULL;
    const gint64 parsed = g_ascii_strtoll(text, &end, 10);
    if (!end || *end != '\0' || parsed <= 0 || parsed > G_MAXINT)
        return FALSE;

    *value = (gint)parsed;
    return TRUE;
}

static gboolean valid_host(const gchar *host)
{
    if (!host || !*host)
        return FALSE;

    for (const gchar *cursor = host; *cursor; cursor++)
    {
        if (!g_ascii_isalnum(*cursor) && *cursor != '.' &&
            *cursor != '-' && *cursor != ':' && *cursor != '_')
            return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    gint channel = 2;
    gint expected_gap_ms = 0;
    gboolean expect_eos = FALSE;
    GOptionEntry options[] = {
        { "channel", 0, 0, G_OPTION_ARG_INT, &channel,
          "RTSP video channel (0-3)", "N" },
        { "expect-gap-ms", 0, 0, G_OPTION_ARG_INT, &expected_gap_ms,
          "Require decoder recovery after this inter-arrival gap", "MS" },
        { "expect-eos", 0, 0, G_OPTION_ARG_NONE, &expect_eos,
          "Treat EOS, rather than timeout, as the expected stop", NULL },
        { NULL }
    };

    GOptionContext *context = g_option_context_new("[HOST [SECONDS]]");
    g_option_context_add_main_entries(context, options, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_printerr("DECODER_ERROR option parsing failed: %s\n",
                   error ? error->message : "unknown");
        if (error)
            g_error_free(error);
        g_option_context_free(context);
        return 1;
    }

    if (argc > 3 || channel < 0 || channel > 3 || expected_gap_ms < 0)
    {
        g_printerr("DECODER_ERROR invalid arguments\n");
        g_option_context_free(context);
        return 1;
    }

    const gchar *host = argc > 1 ? argv[1] : "127.0.0.1";
    gint seconds = 40;
    if (!valid_host(host) ||
        (argc > 2 && !parse_positive_int(argv[2], &seconds)))
    {
        g_printerr("DECODER_ERROR invalid HOST or SECONDS\n");
        g_option_context_free(context);
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    if (!loop)
    {
        g_printerr("DECODER_ERROR failed to create main loop\n");
        g_option_context_free(context);
        return 1;
    }

    State state = {};
    g_mutex_init(&state.lock);
    state.loop = loop;
    state.first_pts = GST_CLOCK_TIME_NONE;
    state.last_pts = GST_CLOCK_TIME_NONE;
    state.latest_idr_pts = GST_CLOCK_TIME_NONE;
    state.recovery.recovery_idr_pts = GST_CLOCK_TIME_NONE;
    state.expect_recovery = expected_gap_ms > 0;
    state.expected_gap_ns = (gint64)expected_gap_ms * GST_MSECOND;

    gchar *launch = g_strdup_printf(
        "rtspsrc location=rtsp://user:user@%s:8554/ch%d protocols=tcp "
        "latency=100 ! rtph265depay ! h265parse ! "
        "video/x-h265,stream-format=byte-stream,alignment=au ! tee name=t "
        "t. ! queue ! appsink name=encoded emit-signals=true sync=false "
        "max-buffers=30 drop=true "
        "t. ! queue ! vpudec ! appsink name=decoded emit-signals=true "
        "sync=false max-buffers=30 drop=true",
        host, channel);
    GstElement *pipeline = gst_parse_launch(launch, &error);
    g_free(launch);
    if (!pipeline)
    {
        g_printerr("DECODER_ERROR pipeline setup failed: %s\n",
                   error ? error->message : "unknown");
        if (error)
            g_error_free(error);
        g_mutex_clear(&state.lock);
        g_main_loop_unref(loop);
        g_option_context_free(context);
        return 1;
    }
    if (error)
    {
        g_printerr("DECODER_ERROR incomplete pipeline setup: %s\n",
                   error->message);
        g_error_free(error);
        gst_object_unref(pipeline);
        g_mutex_clear(&state.lock);
        g_main_loop_unref(loop);
        g_option_context_free(context);
        return 1;
    }

    GstElement *encoded = gst_bin_get_by_name(GST_BIN(pipeline), "encoded");
    GstElement *decoded = gst_bin_get_by_name(GST_BIN(pipeline), "decoded");
    if (!encoded || !decoded)
    {
        g_printerr("DECODER_ERROR appsink setup failed\n");
        if (encoded)
            gst_object_unref(encoded);
        if (decoded)
            gst_object_unref(decoded);
        gst_object_unref(pipeline);
        g_mutex_clear(&state.lock);
        g_main_loop_unref(loop);
        g_option_context_free(context);
        return 1;
    }

    const gulong encoded_handler = g_signal_connect(
        encoded, "new-sample", G_CALLBACK(on_encoded), &state);
    const gulong decoded_handler = g_signal_connect(
        decoded, "new-sample", G_CALLBACK(on_decoded), &state);
    if (encoded_handler == 0 || decoded_handler == 0)
    {
        g_printerr("DECODER_ERROR appsink signal setup failed\n");
        gst_object_unref(encoded);
        gst_object_unref(decoded);
        gst_object_unref(pipeline);
        g_mutex_clear(&state.lock);
        g_main_loop_unref(loop);
        g_option_context_free(context);
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    const guint bus_watch = bus ? gst_bus_add_watch(bus, on_bus, &state) : 0;
    if (bus)
        gst_object_unref(bus);
    state.timeout_source = g_timeout_add_seconds(seconds, on_timeout, &state);
    if (bus_watch == 0 || state.timeout_source == 0)
    {
        g_printerr("DECODER_ERROR event source setup failed\n");
        if (bus_watch)
            g_source_remove(bus_watch);
        if (state.timeout_source)
            g_source_remove(state.timeout_source);
        g_signal_handler_disconnect(encoded, encoded_handler);
        g_signal_handler_disconnect(decoded, decoded_handler);
        gst_object_unref(encoded);
        gst_object_unref(decoded);
        gst_object_unref(pipeline);
        g_mutex_clear(&state.lock);
        g_main_loop_unref(loop);
        g_option_context_free(context);
        return 1;
    }

    g_print("DECODER_START host=%s channel=%d seconds=%d expect_gap_ms=%d "
            "expect_eos=%d\n",
            host, channel, seconds, expected_gap_ms, expect_eos);

    const GstStateChangeReturn start_result = gst_element_set_state(
        pipeline, GST_STATE_PLAYING);
    if (start_result == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("DECODER_ERROR failed to enter PLAYING state\n");
        request_stop(&state, "state-error", TRUE, FALSE);
    }
    else
    {
        g_main_loop_run(loop);
    }

    const GstStateChangeReturn stop_result = gst_element_set_state(
        pipeline, GST_STATE_NULL);
    if (stop_result == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("DECODER_ERROR failed to enter NULL state\n");
        g_mutex_lock(&state.lock);
        state.fatal_bus = TRUE;
        state.stop_reason = "state-error";
        g_mutex_unlock(&state.lock);
    }

    g_mutex_lock(&state.lock);
    const guint timeout_source = state.timeout_source;
    state.timeout_source = 0;
    RtspDecoderValidation validation = {};
    validation.decoded_samples = state.decoded_samples;
    validation.idr_count = state.idr_count;
    validation.recovery_idr_count = state.recovery.recovery_idr_count;
    validation.decoded_after_recovery_idr =
        state.recovery.decoded_after_recovery_idr;
    validation.fatal_bus = state.fatal_bus;
    validation.eos_seen = state.eos_seen;
    validation.expect_eos = expect_eos;
    validation.expect_recovery = state.expect_recovery;
    validation.decoded_before_gap = state.recovery.decoded_before_gap;
    validation.gap_observed = state.recovery.gap_observed;
    validation.expected_gap_ns = state.expected_gap_ns;
    validation.observed_gap_ns = state.recovery.observed_gap_ns;
    validation.stop_reason = state.stop_reason;
    const guint64 encoded_samples = state.encoded_samples;
    const GstClockTime first_pts = state.first_pts;
    const GstClockTime last_pts = state.last_pts;
    const gint64 first_ns = state.first_ns;
    const gint64 last_ns = state.last_ns;
    const gint64 maximum_gap_ns = state.maximum_gap_ns;
    const gboolean decoded_before_gap = state.recovery.decoded_before_gap;
    const gboolean gap_observed = state.recovery.gap_observed;
    g_mutex_unlock(&state.lock);

    if (timeout_source)
        g_source_remove(timeout_source);
    g_source_remove(bus_watch);
    g_signal_handler_disconnect(encoded, encoded_handler);
    g_signal_handler_disconnect(decoded, decoded_handler);

    const gboolean success = rtsp_decoder_validation_success(&validation);
    g_print("DECODER_SUMMARY encoded=%" G_GUINT64_FORMAT
            " decoded=%" G_GUINT64_FORMAT
            " idr_count=%" G_GUINT64_FORMAT
            " gap_observed=%d decoded_before_gap=%d max_gap_ms=%.3f"
            " last_qualifying_gap_ms=%.3f"
            " recovery_idr=%" G_GUINT64_FORMAT
            " decoded_after_recovery_idr=%" G_GUINT64_FORMAT
            " first_pts=%" G_GUINT64_FORMAT
            " last_pts=%" G_GUINT64_FORMAT
            " elapsed_ns=%" G_GINT64_FORMAT
            " stop=%s fatal=%d eos=%d verdict=%s\n",
            encoded_samples, validation.decoded_samples,
            validation.idr_count, gap_observed, decoded_before_gap,
            (gdouble)maximum_gap_ns / GST_MSECOND,
            (gdouble)validation.observed_gap_ns / GST_MSECOND,
            validation.recovery_idr_count,
            validation.decoded_after_recovery_idr,
            first_pts, last_pts,
            last_ns > first_ns ? last_ns - first_ns : 0,
            validation.stop_reason ? validation.stop_reason : "none",
            validation.fatal_bus, validation.eos_seen,
            success ? "PASS" : "FAIL");

    gst_object_unref(encoded);
    gst_object_unref(decoded);
    gst_object_unref(pipeline);
    g_mutex_clear(&state.lock);
    g_main_loop_unref(loop);
    g_option_context_free(context);
    return success ? 0 : 2;
}
