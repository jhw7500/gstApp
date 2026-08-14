/*
 *
 * Cantops recordBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "encoderBin.h"
#include "encoderStat.h"
#include <gst/video/video.h>

static GstPadProbeReturn
drop_no_pts_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf && !GST_BUFFER_PTS_IS_VALID(buf)) {
        __LOG(LOG_INFO, "[GST][%s:%d] dropping buffer without PTS (enc src)", _FILE_, __LINE__);
        return GST_PAD_PROBE_DROP;
    }
    return GST_PAD_PROBE_OK;
}

/* [계측] 채널별 enc 큐/인코더 상태. queue_tune.enc_stat_sec > 0 일 때만 활성화되며
 * 0(기본)이면 프로브도 타이머도 설치하지 않아 상시 비용이 없다.
 * 목적: 4채널 동시 운용의 상한이 VPU 처리량인지(H1) 큐 기아인지(H2) 구분. */
typedef struct {
    GstElement *queue;
    EncoderTelemetry telemetry;
    gint64 enc_last_us;    /* 직전 인코더 출력 시각 (monotonic) */
    gboolean active;
} EncStat;

static EncStat g_encStat[MAX_CHANNEL];

#define ENC_NODATA_WARN_SEC     5   /* 무데이터 감시 주기 */
#define ENC_NODATA_STRIKES      2   /* 다른 채널이 흐르는 중이면 10초만에 경고 */
#define ENC_NODATA_COLD_STRIKES 12  /* 전 채널 무데이터(기동 중)면 60초 유예 */
typedef struct {
    guint8 ch;
    const gchar *stage;
    guint duration_sec;
    gboolean log_frames;
    gint64 start_us;
    guint64 warmup_count;
    guint64 frame_count;
    guint64 invalid_sequence_count;
    guint64 lost_count;
    guint64 sequence_reset_count;
    guint64 pts_backward_count;
    guint64 keyframe_count;
    guint64 pts_hash;
    guint64 first_sequence;
    guint64 previous_sequence;
    guint64 last_sequence;
    GstClockTime first_pts;
    GstClockTime previous_pts;
    GstClockTime last_pts;
    gboolean first_sequence_valid;
    gboolean previous_sequence_valid;
    gboolean previous_pts_valid;
    gboolean videorate_stats;
    guint64 vr_in_start;
    guint64 vr_out_start;
    guint64 vr_drop_start;
    guint64 vr_duplicate_start;
} ChannelSyncTrace;

/* [계측] videorate가 새 offset/PTS를 기록한 뒤에도 원본 V4L2 sequence를
 * 식별하기 위해 입력 buffer에 reference timestamp meta를 붙인다. videorate의
 * buffer copy transform은 이 meta를 보존하므로 영상 memory나 timestamp를
 * 변경하지 않고 실제 선택/제외된 입력 frame을 구분할 수 있다. */
typedef struct {
    guint8 ch;
    guint duration_sec;
    GstCaps *reference;
    GArray *input_sequences;
    GArray *output_sequences;
    guint64 input_count;
    guint64 invalid_input_sequence_count;
    guint64 meta_add_failure_count;
    guint64 output_count;
    guint64 missing_meta_count;
    guint64 origin_sequence_hash;
    guint64 origin_pts_hash;
    GstClockTime first_origin_pts;
    GstClockTime last_origin_pts;
    GstClockTime first_output_pts;
    gboolean started;
    gint finished;
    gint ref_count;
} ChannelRateLineage;

#define CHANNEL_SYNC_FNV_OFFSET G_GUINT64_CONSTANT(1469598103934665603)
#define CHANNEL_SYNC_FNV_PRIME G_GUINT64_CONSTANT(1099511628211)

static guint channel_sync_trace_duration(void);

static guint64 channel_sync_hash_u64(guint64 hash, guint64 value)
{
    for (guint i = 0; i < sizeof(value); i++) {
        hash ^= (value >> (i * 8)) & 0xff;
        hash *= CHANNEL_SYNC_FNV_PRIME;
    }
    return hash;
}

static void channel_rate_lineage_unref(gpointer user_data)
{
    ChannelRateLineage *lineage = (ChannelRateLineage *)user_data;

    if (!lineage || !g_atomic_int_dec_and_test(&lineage->ref_count))
        return;

    if (lineage->reference)
        gst_caps_unref(lineage->reference);
    if (lineage->input_sequences)
        g_array_free(lineage->input_sequences, TRUE);
    if (lineage->output_sequences)
        g_array_free(lineage->output_sequences, TRUE);
    g_free(lineage);
}

static GstPadProbeReturn
channel_rate_lineage_input_probe(GstPad *pad, GstPadProbeInfo *info,
                                 gpointer user_data)
{
    ChannelRateLineage *lineage = (ChannelRateLineage *)user_data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (!lineage || !buffer)
        return GST_PAD_PROBE_OK;
    if (g_atomic_int_get(&lineage->finished))
        return GST_PAD_PROBE_REMOVE;

    lineage->input_count++;
    if (!GST_BUFFER_OFFSET_IS_VALID(buffer)) {
        lineage->invalid_input_sequence_count++;
        return GST_PAD_PROBE_OK;
    }

    guint64 sequence = GST_BUFFER_OFFSET(buffer);
    g_array_append_val(lineage->input_sequences, sequence);

    buffer = gst_buffer_make_writable(buffer);
    if (!buffer) {
        lineage->meta_add_failure_count++;
        return GST_PAD_PROBE_OK;
    }
    GST_PAD_PROBE_INFO_DATA(info) = buffer;

    GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (!gst_buffer_add_reference_timestamp_meta(
            buffer, lineage->reference, sequence, pts))
        lineage->meta_add_failure_count++;

    return GST_PAD_PROBE_OK;
}

static void channel_rate_lineage_summary(ChannelRateLineage *lineage)
{
    GHashTable *selected = g_hash_table_new_full(g_int64_hash, g_int64_equal,
                                                 g_free, NULL);
    guint64 first_input_sequence = 0;
    guint64 last_decided_sequence = 0;
    guint64 first_output_sequence = 0;
    guint64 last_output_sequence = 0;
    guint64 duplicate_count = 0;
    guint64 backwards_count = 0;
    guint64 dropped_count = 0;
    guint64 previous_output_sequence = 0;
    gboolean previous_output_valid = FALSE;
    GString *dropped = g_string_new(NULL);
    GString *duplicated = g_string_new(NULL);

    if (lineage->input_sequences->len > 0)
        first_input_sequence = g_array_index(lineage->input_sequences,
                                             guint64, 0);

    for (guint i = 0; i < lineage->output_sequences->len; i++) {
        guint64 sequence = g_array_index(lineage->output_sequences, guint64, i);
        if (i == 0)
            first_output_sequence = sequence;
        last_output_sequence = sequence;

        if (previous_output_valid) {
            if (sequence == previous_output_sequence) {
                duplicate_count++;
                if (duplicate_count <= 32) {
                    if (duplicated->len > 0)
                        g_string_append_c(duplicated, ',');
                    g_string_append_printf(duplicated, "%"
                                           G_GUINT64_FORMAT, sequence);
                }
            } else if (sequence < previous_output_sequence) {
                backwards_count++;
            }
        }
        previous_output_sequence = sequence;
        previous_output_valid = TRUE;

        guint64 lookup = sequence;
        gpointer value = g_hash_table_lookup(selected, &lookup);
        guint count = GPOINTER_TO_UINT(value);
        guint64 *key = g_new(guint64, 1);
        *key = sequence;
        g_hash_table_replace(selected, key, GUINT_TO_POINTER(count + 1));
    }

    /* videorate는 다음 입력과 비교한 뒤 이전 frame을 출력하므로 마지막 입력은
     * 아직 pending일 수 있다. 마지막으로 실제 출력된 원본 sequence까지만
     * 결정 완료 구간으로 보고 drop 여부를 계산한다. */
    last_decided_sequence = last_output_sequence;
    for (guint i = 0; i < lineage->input_sequences->len; i++) {
        guint64 sequence = g_array_index(lineage->input_sequences, guint64, i);
        if (sequence < first_input_sequence || sequence > last_decided_sequence)
            continue;
        if (g_hash_table_lookup(selected, &sequence))
            continue;

        dropped_count++;
        if (dropped_count <= 32) {
            if (dropped->len > 0)
                g_string_append_c(dropped, ',');
            g_string_append_printf(dropped, "%" G_GUINT64_FORMAT, sequence);
        }
    }

    __LOG(LOG_NOTICE,
          "[RATE_LINEAGE][%s:%d] ch=%u summary inputs=%" G_GUINT64_FORMAT
          " input_seq=%" G_GUINT64_FORMAT "..%" G_GUINT64_FORMAT
          " outputs=%" G_GUINT64_FORMAT " output_origin_seq=%"
          G_GUINT64_FORMAT "..%" G_GUINT64_FORMAT
          " decided_last_seq=%" G_GUINT64_FORMAT " dropped=%"
          G_GUINT64_FORMAT " dropped_seq=%s duplicates=%" G_GUINT64_FORMAT
          " duplicate_seq=%s backwards=%" G_GUINT64_FORMAT
          " missing_meta=%"
          G_GUINT64_FORMAT " invalid_input_seq=%" G_GUINT64_FORMAT
          " meta_add_failures=%" G_GUINT64_FORMAT
          " origin_seq_hash=%016" G_GINT64_MODIFIER
          "x origin_pts=%" G_GUINT64_FORMAT "..%" G_GUINT64_FORMAT
          " origin_pts_hash=%016" G_GINT64_MODIFIER "x",
          _FILE_, __LINE__, lineage->ch, lineage->input_count,
          first_input_sequence, last_decided_sequence, lineage->output_count,
          first_output_sequence, last_output_sequence, last_decided_sequence,
          dropped_count, dropped->len > 0 ? dropped->str : "none",
          duplicate_count,
          duplicated->len > 0 ? duplicated->str : "none", backwards_count,
          lineage->missing_meta_count,
          lineage->invalid_input_sequence_count,
          lineage->meta_add_failure_count, lineage->origin_sequence_hash,
          lineage->first_origin_pts, lineage->last_origin_pts,
          lineage->origin_pts_hash);

    g_string_free(dropped, TRUE);
    g_string_free(duplicated, TRUE);
    g_hash_table_destroy(selected);
}

static GstPadProbeReturn
channel_rate_lineage_output_probe(GstPad *pad, GstPadProbeInfo *info,
                                  gpointer user_data)
{
    ChannelRateLineage *lineage = (ChannelRateLineage *)user_data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (!lineage || !buffer)
        return GST_PAD_PROBE_OK;
    if (g_atomic_int_get(&lineage->finished))
        return GST_PAD_PROBE_REMOVE;

    GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (!lineage->started) {
        if (!GST_BUFFER_PTS_IS_VALID(buffer) || pts == 0)
            return GST_PAD_PROBE_OK;
        lineage->started = TRUE;
        lineage->first_output_pts = pts;
    }

    if (GST_BUFFER_PTS_IS_VALID(buffer) && pts >= lineage->first_output_pts &&
        pts - lineage->first_output_pts >=
            (GstClockTime)lineage->duration_sec * GST_SECOND) {
        channel_rate_lineage_summary(lineage);
        g_atomic_int_set(&lineage->finished, TRUE);
        return GST_PAD_PROBE_REMOVE;
    }

    lineage->output_count++;
    GstReferenceTimestampMeta *meta =
        gst_buffer_get_reference_timestamp_meta(buffer, lineage->reference);
    if (!meta) {
        lineage->missing_meta_count++;
        return GST_PAD_PROBE_OK;
    }

    guint64 origin_sequence = meta->timestamp;
    g_array_append_val(lineage->output_sequences, origin_sequence);
    lineage->origin_sequence_hash = channel_sync_hash_u64(
        lineage->origin_sequence_hash, origin_sequence);
    if (lineage->output_sequences->len == 1)
        lineage->first_origin_pts = meta->duration;
    lineage->last_origin_pts = meta->duration;
    lineage->origin_pts_hash =
        channel_sync_hash_u64(lineage->origin_pts_hash, meta->duration);

    return GST_PAD_PROBE_OK;
}

static void install_channel_rate_lineage(GstElement *rate, guint8 ch)
{
    guint duration_sec = channel_sync_trace_duration();
    if (duration_sec == 0)
        return;

    GstPad *sink_pad = gst_element_get_static_pad(rate, "sink");
    GstPad *src_pad = gst_element_get_static_pad(rate, "src");
    if (!sink_pad || !src_pad) {
        __LOG(LOG_ERR, "[RATE_LINEAGE][%s:%d] ch=%u videorate pad is NULL",
              _FILE_, __LINE__, ch);
        if (sink_pad)
            gst_object_unref(sink_pad);
        if (src_pad)
            gst_object_unref(src_pad);
        return;
    }

    ChannelRateLineage *lineage = g_new0(ChannelRateLineage, 1);
    lineage->ch = ch;
    lineage->duration_sec = duration_sec;
    lineage->reference = gst_caps_new_simple(
        "timestamp/x-gstapp-v4l2-sequence", "channel", G_TYPE_INT,
        (gint)ch, NULL);
    lineage->input_sequences = g_array_new(FALSE, FALSE, sizeof(guint64));
    lineage->output_sequences = g_array_new(FALSE, FALSE, sizeof(guint64));
    lineage->origin_sequence_hash = CHANNEL_SYNC_FNV_OFFSET;
    lineage->origin_pts_hash = CHANNEL_SYNC_FNV_OFFSET;
    lineage->ref_count = 2;

    gulong sink_probe_id = gst_pad_add_probe(
        sink_pad, GST_PAD_PROBE_TYPE_BUFFER, channel_rate_lineage_input_probe,
        lineage, channel_rate_lineage_unref);
    gulong src_probe_id = gst_pad_add_probe(
        src_pad, GST_PAD_PROBE_TYPE_BUFFER, channel_rate_lineage_output_probe,
        lineage, channel_rate_lineage_unref);

    gst_object_unref(sink_pad);
    gst_object_unref(src_pad);

    if (sink_probe_id == 0 || src_probe_id == 0) {
        if (sink_probe_id == 0)
            channel_rate_lineage_unref(lineage);
        if (src_probe_id == 0)
            channel_rate_lineage_unref(lineage);
        __LOG(LOG_ERR,
              "[RATE_LINEAGE][%s:%d] ch=%u failed to install probes sink=%lu"
              " src=%lu",
              _FILE_, __LINE__, ch, sink_probe_id, src_probe_id);
        return;
    }

    __LOG(LOG_NOTICE,
          "[RATE_LINEAGE][%s:%d] ch=%u enabled duration_sec=%u",
          _FILE_, __LINE__, ch, duration_sec);
}

static guint channel_sync_trace_duration(void)
{
    return cmdArg.channel_sync_trace_sec > 0
               ? (guint)cmdArg.channel_sync_trace_sec
               : 0;
}

static GstPadProbeReturn
channel_sync_trace_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    ChannelSyncTrace *trace = (ChannelSyncTrace *)user_data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    gint64 now_us = g_get_monotonic_time();

    if (!trace || !buffer)
        return GST_PAD_PROBE_OK;

    guint64 sequence = GST_BUFFER_OFFSET(buffer);
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    gboolean sequence_valid = GST_BUFFER_OFFSET_IS_VALID(buffer);
    gboolean pts_valid = GST_BUFFER_PTS_IS_VALID(buffer);

    /* The V4L2 source can emit zero-PTS startup buffers before establishing
     * its timestamp/sequence bases. They are warm-up, not transport loss. */
    if (trace->start_us == 0) {
        if (!pts_valid || pts == 0) {
            trace->warmup_count++;
            return GST_PAD_PROBE_OK;
        }

        trace->start_us = now_us;
        trace->first_pts = pts;
        if (trace->videorate_stats) {
            GstElement *element = gst_pad_get_parent_element(pad);
            if (element) {
                g_object_get(element, "in", &trace->vr_in_start,
                             "out", &trace->vr_out_start,
                             "drop", &trace->vr_drop_start,
                             "duplicate", &trace->vr_duplicate_start, NULL);
                gst_object_unref(element);
            }
        }
    }

    if (pts_valid && pts >= trace->first_pts &&
        pts - trace->first_pts >= (GstClockTime)trace->duration_sec * GST_SECOND) {
        GstClockTime pts_span = trace->previous_pts_valid
                                    ? trace->last_pts - trace->first_pts
                                    : GST_CLOCK_TIME_NONE;
        guint64 vr_in = 0, vr_out = 0, vr_drop = 0, vr_duplicate = 0;
        if (trace->videorate_stats) {
            GstElement *element = gst_pad_get_parent_element(pad);
            if (element) {
                g_object_get(element, "in", &vr_in, "out", &vr_out,
                             "drop", &vr_drop, "duplicate", &vr_duplicate,
                             NULL);
                gst_object_unref(element);
            }
        }

        __LOG(LOG_NOTICE,
              "[CHANNEL_SYNC][%s:%d] ch=%u stage=%s summary duration_pts_ns=%"
              G_GUINT64_FORMAT " duration_mono_us=%" G_GINT64_FORMAT
              " warmup_skipped=%" G_GUINT64_FORMAT " frames=%"
              G_GUINT64_FORMAT " first_seq_valid=%d first_seq=%"
              G_GUINT64_FORMAT
              " last_seq=%" G_GUINT64_FORMAT " invalid_seq=%"
              G_GUINT64_FORMAT " lost=%" G_GUINT64_FORMAT
              " sequence_resets=%" G_GUINT64_FORMAT " pts_backwards=%"
              G_GUINT64_FORMAT " keyframes=%" G_GUINT64_FORMAT
              " first_pts_ns=%" G_GUINT64_FORMAT " last_pts_ns=%"
              G_GUINT64_FORMAT " pts_hash=%016" G_GINT64_MODIFIER
              "x vr_in=%" G_GUINT64_FORMAT " vr_out=%" G_GUINT64_FORMAT
              " vr_drop=%" G_GUINT64_FORMAT " vr_duplicate=%"
              G_GUINT64_FORMAT,
              _FILE_, __LINE__, trace->ch, trace->stage, pts_span,
              now_us - trace->start_us, trace->warmup_count,
              trace->frame_count, trace->first_sequence_valid,
              trace->first_sequence, trace->last_sequence,
              trace->invalid_sequence_count, trace->lost_count,
              trace->sequence_reset_count, trace->pts_backward_count,
              trace->keyframe_count, trace->first_pts, trace->last_pts,
              trace->pts_hash,
              trace->videorate_stats ? vr_in - trace->vr_in_start : 0,
              trace->videorate_stats ? vr_out - trace->vr_out_start : 0,
              trace->videorate_stats ? vr_drop - trace->vr_drop_start : 0,
              trace->videorate_stats
                  ? vr_duplicate - trace->vr_duplicate_start
                  : 0);
        return GST_PAD_PROBE_REMOVE;
    }

    gint64 sequence_delta = -1;
    gint64 pts_delta_ns = -1;

    if (sequence_valid) {
        if (!trace->first_sequence_valid) {
            trace->first_sequence = sequence;
            trace->first_sequence_valid = TRUE;
        }
        if (trace->previous_sequence_valid) {
            if (sequence >= trace->previous_sequence) {
                sequence_delta = (gint64)(sequence - trace->previous_sequence);
                if (sequence_delta > 1)
                    trace->lost_count += (guint64)(sequence_delta - 1);
            } else {
                trace->sequence_reset_count++;
            }
        }
        trace->last_sequence = sequence;
    } else {
        trace->invalid_sequence_count++;
    }

    if (pts_valid && trace->previous_pts_valid) {
        pts_delta_ns = GST_CLOCK_DIFF(trace->previous_pts, pts);
        if (pts_delta_ns < 0)
            trace->pts_backward_count++;
    }

    trace->frame_count++;
    trace->pts_hash = channel_sync_hash_u64(trace->pts_hash, pts);
    if (!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
        trace->keyframe_count++;
    if (trace->log_frames) {
        __LOG(LOG_NOTICE,
              "[CHANNEL_SYNC][%s:%d] ch=%u stage=%s sample=%"
              G_GUINT64_FORMAT " mono_ns=%" G_GINT64_FORMAT
              " seq_valid=%d seq=%" G_GUINT64_FORMAT " seq_delta=%"
              G_GINT64_FORMAT " pts_valid=%d pts_ns=%" G_GUINT64_FORMAT
              " pts_delta_ns=%" G_GINT64_FORMAT " lost_total=%"
              G_GUINT64_FORMAT,
              _FILE_, __LINE__, trace->ch, trace->stage, trace->frame_count,
              now_us * 1000, sequence_valid, sequence, sequence_delta,
              pts_valid, pts, pts_delta_ns, trace->lost_count);
    }

    trace->previous_sequence = sequence;
    trace->previous_pts = pts;
    trace->previous_sequence_valid = sequence_valid;
    trace->previous_pts_valid = pts_valid;
    if (pts_valid)
        trace->last_pts = pts;

    return GST_PAD_PROBE_OK;
}

static void install_channel_sync_trace_pad(GstPad *pad, guint8 ch,
                                           const gchar *stage,
                                           gboolean log_frames,
                                           gboolean videorate_stats)
{
    guint duration_sec = channel_sync_trace_duration();
    if (duration_sec == 0)
        return;

    if (!pad) {
        __LOG(LOG_ERR, "[CHANNEL_SYNC][%s:%d] ch=%u stage=%s pad is NULL",
              _FILE_, __LINE__, ch, stage);
        return;
    }

    ChannelSyncTrace *trace = g_new0(ChannelSyncTrace, 1);
    trace->ch = ch;
    trace->stage = stage;
    trace->duration_sec = duration_sec;
    trace->log_frames = log_frames;
    trace->videorate_stats = videorate_stats;
    trace->pts_hash = CHANNEL_SYNC_FNV_OFFSET;

    gulong probe_id = gst_pad_add_probe(
        pad, GST_PAD_PROBE_TYPE_BUFFER, channel_sync_trace_probe, trace,
        (GDestroyNotify)g_free);

    if (probe_id == 0) {
        g_free(trace);
        __LOG(LOG_ERR,
              "[CHANNEL_SYNC][%s:%d] ch=%u stage=%s failed to install probe",
              _FILE_, __LINE__, ch, stage);
        return;
    }

    __LOG(LOG_NOTICE,
          "[CHANNEL_SYNC][%s:%d] ch=%u stage=%s enabled duration_sec=%u"
          " frame_log=%d",
          _FILE_, __LINE__, ch, stage, duration_sec, log_frames);
}

static void install_channel_sync_trace(GstElement *element,
                                       const gchar *pad_name, guint8 ch,
                                       const gchar *stage,
                                       gboolean log_frames,
                                       gboolean videorate_stats = FALSE)
{
    if (channel_sync_trace_duration() == 0)
        return;

    GstPad *pad = gst_element_get_static_pad(element, pad_name);
    install_channel_sync_trace_pad(pad, ch, stage, log_frames,
                                   videorate_stats);
    if (pad)
        gst_object_unref(pad);
}

/* 활성 채널 수 (메모리 예산 분배용) */
static guint enc_active_channels(void)
{
    guint n = 0;
    for (guint i = 0; i < MAX_CHANNEL; i++)
        if (cmdArg.cam[i].enable)
            n++;
    return n ? n : 1;
}

/* 무데이터 감시용으로 항상 설치된다. 평시 비용은 증가 연산 1회.
 * 레벨 워터마크 샘플링은 계측이 켜졌을 때만 수행한다. */
static GstPadProbeReturn
enc_q_in_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    EncStat *s = &g_encStat[GPOINTER_TO_INT(user_data)];
    s->telemetry.recordQueueInput();
    /* 큐가 가장 찬 시점에 근접한 샘플 (삽입 직전이라 실제보다 1 작을 수 있음) */
    if (cmdArg.queue_enc_stat_sec > 0 && s->queue) {
        guint lvl = 0;
        g_object_get(s->queue, "current-level-buffers", &lvl, NULL);
        s->telemetry.recordQueueLevel(lvl);
    }
    return GST_PAD_PROBE_OK;
}

/* [무데이터 감시] 소스가 죽어 프레임이 한 장도 안 들어오는 채널을 찾아낸다.
 * 그 채널의 sink 는 preroll 을 못 하고, 그러면 파이프라인 전체가 PAUSED 에
 * 고착되어 '파일이 안 생김' 으로만 드러난다. 원인 채널을 바로 지목하기 위한 감시. */
static gboolean enc_nodata_watch(gpointer user_data)
{
    static guint64 prev[MAX_CHANNEL];
    static guint strike[MAX_CHANNEL];
    static gboolean warned[MAX_CHANNEL];
    static gboolean any_data_seen = FALSE;
    guint64 q_in[MAX_CHANNEL];

    for (gint i = 0; i < MAX_CHANNEL; i++) {
        EncoderTelemetrySnapshot snapshot =
            g_encStat[i].telemetry.snapshot(FALSE);
        q_in[i] = snapshot.q_in;
    }

    if (!any_data_seen) {
        for (gint i = 0; i < MAX_CHANNEL; i++) {
            if (q_in[i] > 0) {
                any_data_seen = TRUE;
                break;
            }
        }
    }

    for (gint i = 0; i < MAX_CHANNEL; i++) {
        EncStat *s = &g_encStat[i];
        if (!s->active || !cmdArg.cam[i].enable)
            continue;
        if ((g_link_disconnect_mask >> i) & 1) {  /* 링크 끊김은 별도 감시 대상 */
            strike[i] = 0;
            prev[i] = q_in[i];
            continue;
        }

        guint64 cur = q_in[i];
        if (cur != prev[i]) {
            if (warned[i]) {
                __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d data resumed (in=%" G_GUINT64_FORMAT ")",
                      _FILE_, __LINE__, i, cur);
                warned[i] = FALSE;
            }
            strike[i] = 0;
            prev[i] = cur;
            continue;
        }

        strike[i]++;
        /* 다른 채널이 이미 흐르고 있으면 그 채널만 죽은 것이므로 빨리 경고하고,
         * 아직 아무 채널도 데이터가 없으면 기동 지연(play delay)일 수 있어 길게 기다린다. */
        guint limit = any_data_seen ? ENC_NODATA_STRIKES : ENC_NODATA_COLD_STRIKES;
        if (strike[i] < limit)
            continue;
        if (warned[i] && (strike[i] % (30 / ENC_NODATA_WARN_SEC)) != 0)
            continue;  /* 첫 경고 후에는 30초 간격으로만 반복 */

        __LOG(LOG_ERR,
              "[GST][%s:%d] ch%d NO DATA for %ds - zero frames reached enc-queue"
              " (csi%d source not delivering). This channel's sink cannot preroll,"
              " which stalls the whole pipeline in PAUSED and produces no files.",
              _FILE_, __LINE__, i, strike[i] * ENC_NODATA_WARN_SEC, i / 2);
        warned[i] = TRUE;
    }
    return G_SOURCE_CONTINUE;
}

static GstPadProbeReturn
enc_q_out_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    g_encStat[GPOINTER_TO_INT(user_data)].telemetry.recordQueueOutput();
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
enc_out_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    EncStat *s = &g_encStat[GPOINTER_TO_INT(user_data)];
    gint64 now = g_get_monotonic_time();
    gint64 gap = s->enc_last_us ? now - s->enc_last_us : 0;
    s->telemetry.recordEncoderOutput(gap);
    s->enc_last_us = now;
    return GST_PAD_PROBE_OK;
}

static void enc_queue_overrun_cb(GstElement *queue, gpointer user_data)
{
    g_encStat[GPOINTER_TO_INT(user_data)].telemetry.recordOverrun();
}

/* 주기 리포트: 직전 구간 증분과 누적 워터마크를 함께 찍는다. */
static gboolean enc_stat_report(gpointer user_data)
{
    static guint64 prev_in[MAX_CHANNEL], prev_out[MAX_CHANNEL];
    static guint64 prev_enc[MAX_CHANNEL], prev_over[MAX_CHANNEL];
    gint period = cmdArg.queue_enc_stat_sec > 0 ? cmdArg.queue_enc_stat_sec : 1;

    for (gint i = 0; i < MAX_CHANNEL; i++) {
        EncStat *s = &g_encStat[i];
        if (!s->active || !s->queue)
            continue;

        guint lvl_buf = 0, lvl_byte = 0;
        guint max_bytes = 0;
        g_object_get(s->queue, "current-level-buffers", &lvl_buf,
                     "current-level-bytes", &lvl_byte,
                     "max-size-bytes", &max_bytes, NULL);

        EncoderTelemetrySnapshot snapshot = s->telemetry.snapshot(TRUE);
        guint64 d_in = snapshot.q_in - prev_in[i];
        guint64 d_out = snapshot.q_out - prev_out[i];
        guint64 d_enc = snapshot.enc_out - prev_enc[i];
        guint64 d_over = snapshot.overrun - prev_over[i];
        /* q_in/q_out 은 각각 원자적이지만 하나의 트랜잭션으로 읽히지는 않으므로
         * out 이 in 을 한 개 앞설 수 있다. 음수 leak 은 0 으로 본다. */
        gint64 d_leak = (gint64)d_in - (gint64)d_out;
        if (d_leak < 0)
            d_leak = 0;
        prev_in[i] = snapshot.q_in;
        prev_out[i] = snapshot.q_out;
        prev_enc[i] = snapshot.enc_out;
        prev_over[i] = snapshot.overrun;

        __LOG(LOG_NOTICE,
              "[GST][%s:%d] ch%d enc-stat[%ds] in=%" G_GUINT64_FORMAT "(%.1ffps)"
              " out=%" G_GUINT64_FORMAT " leak=%" G_GINT64_FORMAT
              " enc=%" G_GUINT64_FORMAT "(%.1ffps) overrun=%" G_GUINT64_FORMAT
              " lvl=%u buf/%u B (max %u buf) max-bytes=%u enc_gap_max=%.1fms",
              _FILE_, __LINE__, i, period,
              d_in, (gdouble)d_in / period,
              d_out, d_leak,
              d_enc, (gdouble)d_enc / period,
              d_over, lvl_buf, lvl_byte, snapshot.lvl_buf_max, max_bytes,
              snapshot.enc_gap_max_us / 1000.0);
    }
    return G_SOURCE_CONTINUE;
}

/* [진단/튜닝] enc 큐 sink pad 의 negotiated caps 를 채널당 1회 읽어
 *  (1) 실효 큐 깊이를 기록하고,
 *  (2) queue_tune.enc_src_frames > 0 이면 실제 프레임 크기 기반으로 큐를 재사이징한다.
 * 큐는 crop 앞이라 프레임은 CSI 통짜(width*2 x height)이고, dual(GPU crop) 경로는
 * RGBx 32bpp 라 FHD 에서는 기본 max-size-bytes(10MB)가 단일 프레임보다 작아진다. */
static GstPadProbeReturn
enc_queue_caps_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    gint ch = GPOINTER_TO_INT(user_data);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        return GST_PAD_PROBE_OK;  /* 아직 negotiate 전 - 다음 버퍼에서 재시도 */

    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    gsize frame_bytes = buf ? gst_buffer_get_size(buf) : 0;

    GstStructure *s = gst_caps_get_structure(caps, 0);
    gint w = 0, h = 0, fps_n = 0, fps_d = 1;
    const gchar *fmt = gst_structure_get_string(s, "format");
    gst_structure_get_int(s, "width", &w);
    gst_structure_get_int(s, "height", &h);
    gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d);

    GstElement *queue = gst_pad_get_parent_element(pad);

    /* (2) 프레임 수 기준 동적 사이징 */
    if (queue && frame_bytes > 0 && cmdArg.queue_enc_src_frames > 0) {
        gint n = CLAMP(cmdArg.queue_enc_src_frames,
                       QUEUE_ENC_FRAMES_MIN, QUEUE_ENC_FRAMES_MAX);
        gint n_req = n;
        const gchar *capped = "none";

        if (cmdArg.queue_enc_budget_mb > 0) {
            guint64 budget = (guint64)cmdArg.queue_enc_budget_mb * 1024 * 1024;
            guint64 per_ch = budget / enc_active_channels();
            gint n_budget = (gint)(per_ch / frame_bytes);
            if (n_budget < QUEUE_ENC_FRAMES_MIN)
                n_budget = QUEUE_ENC_FRAMES_MIN;
            if (n_budget < n) {
                n = n_budget;
                capped = "budget";
            }
        }

        guint64 want = (guint64)n * (guint64)frame_bytes;
        /* 구속조건을 바이트 하나로 통일 - max-size-time 의 침묵 override 제거 */
        g_object_set(queue, "max-size-bytes", (guint)want,
                            "max-size-time", (guint64)0, NULL);
        __LOG(LOG_NOTICE,
              "[GST][%s:%d] ch%d enc-queue resize: %d frames requested -> %d applied"
              " (%" G_GUINT64_FORMAT " B, cap=%s, budget=%dMB/%uch)",
              _FILE_, __LINE__, ch, n_req, n, want, capped,
              cmdArg.queue_enc_budget_mb, enc_active_channels());
    }

    guint max_bytes = 0;
    guint64 max_time = 0;
    if (queue)
        g_object_get(queue, "max-size-bytes", &max_bytes, "max-size-time", &max_time, NULL);

    gchar *caps_str = gst_caps_to_string(caps);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d enc-queue caps: %s", _FILE_, __LINE__, ch, caps_str);
    g_free(caps_str);

    /* (1) 바이트/시간 상한이 각각 몇 프레임 = 몇 ms 인지 환산해 실효 깊이 확정 */
    if (frame_bytes > 0 && fps_n > 0) {
        gdouble frames = (gdouble)max_bytes / (gdouble)frame_bytes;
        gdouble bytes_ms = frames * 1000.0 * (gdouble)fps_d / (gdouble)fps_n;
        gdouble time_ms = (max_time > 0) ? (gdouble)max_time / (gdouble)GST_MSECOND : G_MAXDOUBLE;
        __LOG(LOG_NOTICE,
              "[GST][%s:%d] ch%d enc-queue depth: %dx%d %s fps=%d/%d frame=%" G_GSIZE_FORMAT "B"
              " | max-bytes=%u(%.2f frames=%.0fms) max-time=%s -> effective %.0fms (%s limited)",
              _FILE_, __LINE__, ch, w, h, fmt ? fmt : "?", fps_n, fps_d, frame_bytes,
              max_bytes, frames, bytes_ms,
              (max_time > 0) ? "set" : "disabled",
              MIN(bytes_ms, time_ms), (bytes_ms <= time_ms) ? "bytes" : "time");
    }

    if (queue)
        gst_object_unref(queue);
    gst_caps_unref(caps);
    return GST_PAD_PROBE_REMOVE;  /* 1회만 */
}

EncoderBin* EncoderBin::getInstance()
{
	static EncoderBin instance;
	return &instance;
}

void EncoderBin::setOverlayText(gchar *text)
{
    g_object_set(re.overlay, "text", text, NULL);
}

void EncoderBin::getBitrate()
{
    gint bps;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
     g_print("rec ch%d get bitrate : %d\n", encData.ch, bps);
}

void EncoderBin::setBitrate(gint data)
{
    gint bps;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    g_object_set(re.enc, "bitrate", data, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set bitrate : %d", _FILE_, __LINE__, encData.ch, bps);
    g_print("rec ch%d set bitrate : %d\n", encData.ch, bps);
}

void EncoderBin::getFps()
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

    g_object_get(re.capsfilter, "caps", &caps, NULL);
    caps_str = gst_caps_to_string(caps);

    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch, caps_str);
    g_print("rec ch%d get caps : %s\n", encData.ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);
}

void EncoderBin::setFps(guint16 data)
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

    if (re.capsfilter == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d capsfilter not initialized", _FILE_, __LINE__, encData.ch);
        return;
    }

#if 0
    g_object_get(re.rate, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get max-rate : %d", _FILE_, __LINE__, ch, fps);
    g_object_set(re.rate, "max-rate", data, NULL);
    g_object_get(re.rate, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set max-rate : %d", _FILE_, __LINE__, ch, fps);
#endif

    //g_object_get(re.capsfilter, "caps", caps, NULL);
    //caps_str = gst_caps_to_string(caps);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch, caps_str);
    caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, data, 1, NULL);
    g_object_set(re.capsfilter, "caps", caps, NULL);
    //g_object_get(re.capsfilter, "caps", caps, NULL);
    caps_str = gst_caps_to_string(caps);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set caps : %s", _FILE_, __LINE__, encData.ch, caps_str);
    g_print("rec ch%d set caps : %s\n", encData.ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);
}

void EncoderBin::setRotation(guint16 data)
{
    gint rotation;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.convert, "rotation", data, NULL);
    g_object_get(re.convert, "rotation", &rotation, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set rotation : %d", _FILE_, __LINE__, encData.ch, rotation);
    g_print("rec ch%d set rotation : %d\n", encData.ch, rotation);
}

void EncoderBin::getRotation()
{
    gint rotation;

    g_object_get(re.convert, "rotation", &rotation, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get rotation : %d", _FILE_, __LINE__, ch, rotation);
    g_print("rec ch%d get rotation : %d\n", encData.ch, rotation);
}

void EncoderBin::setGop(guint16 data)
{
    gint gop;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "gop-size", data, NULL);
    g_object_get(re.enc, "gop-size", &gop, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set gop_size : %d", _FILE_, __LINE__, encData.ch, gop);
    g_print("rec ch%d set gop_size : %d\n", encData.ch, gop);
}

void EncoderBin::getGop()
{
    gint gop;

    g_object_get(re.enc, "gop-size", &gop, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get gop_size : %d", _FILE_, __LINE__, ch, gop);
    g_print("rec ch%d get gop_size : %d\n", encData.ch, gop);
}

void EncoderBin::getKeyframe()
{
    gint key;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get keyframe : %d", _FILE_, __LINE__, ch, key);
    g_print("rec ch%d get keyframe : %d\n", encData.ch, key);
}

void EncoderBin::setkeyframe(guint16 data)
{
    gint key;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "set-keyframe", data, NULL);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set keyframe : %d", _FILE_, __LINE__, encData.ch, key);
    g_print("rec ch%d set keyframe : %d\n", encData.ch, key);
}

void EncoderBin::forceKeyframe()
{
    if (re.enc == NULL) return;

    // 업스트림 방향으로 force-keyunit 이벤트 전송
    // timestamp: GST_CLOCK_TIME_NONE (현재 시점), count: 1 (즉시), flags: 0
    GstEvent *event = gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0);
    if (gst_element_send_event(re.enc, event)) {
        __LOG(LOG_INFO, "[GST][%s:%d] ch%d Force keyframe event sent", _FILE_, __LINE__, encData.ch);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d Failed to send force keyframe event", _FILE_, __LINE__, encData.ch);
    }
}

GstState EncoderBin::getState()
{
    GstState state;

    gst_element_get_state(re.bin, &state, NULL, GST_CLOCK_TIME_NONE);
 
    return state;
}

GstStateChangeReturn EncoderBin::setState(GstState state)
{
    return gst_element_set_state(re.bin , state);
}

gboolean EncoderBin::addBinToPipe(GstElement *pipe)
{
    if(gst_bin_get_by_name(GST_BIN(pipe), g_strdup_printf("captureBin%d", encData.ch)) != NULL)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d capture bin is already added", _FILE_, __LINE__, encData.ch);
        return 1;
    }

    return gst_bin_add(GST_BIN(pipe), re.bin);
}

gboolean EncoderBin::removeBinToPipe(GstElement *pipe)
{
    //gst_element_set_state(be.bin, GST_STATE_NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, encData.ch, __FUNCTION__);

    return gst_bin_remove(GST_BIN(pipe), re.bin);
}

GstPad* EncoderBin::getBinSinkPad()
{
    if(sinkPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, encData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, encData.ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gboolean EncoderBin::addBinRtspSrcPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, encData.ch, __FUNCTION__);
    GstPad *target_pad = gst_element_get_request_pad(re.tee, "src_%u");
    srcRtspPad = gst_ghost_pad_new("rtsp_srcpad", target_pad);
    if (target_pad)
        gst_object_unref(target_pad);

    install_channel_sync_trace_pad(srcRtspPad, encData.ch, "rtsp_branch",
                                   FALSE, FALSE);

    return gst_element_add_pad(re.bin, srcRtspPad);
}

GstPad* EncoderBin::getBinRtspSrcPad()
{
    if(srcRtspPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, encData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, encData.ch);

    return srcRtspPad;
}

gboolean EncoderBin::addBinRecSrcPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, encData.ch);
    GstPad *target_pad = gst_element_get_request_pad(re.tee, "src_%u");
    srcRecPad = gst_ghost_pad_new("rec_srcpad", target_pad);
    if (target_pad)
        gst_object_unref(target_pad);

    install_channel_sync_trace_pad(srcRecPad, encData.ch, "record_branch",
                                   FALSE, FALSE);

    return gst_element_add_pad(re.bin, srcRecPad);
}

GstPad* EncoderBin::getBinRecSrcPad()
{
    if(srcRecPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, encData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, encData.ch);

    return srcRecPad;
}


EncoderBin::EncoderBin()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    re.bin = NULL;
    encData.debug = FALSE;
}

// RecordBin 클래스의 소멸자 정의
EncoderBin::~EncoderBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, encData.ch);
}

gboolean EncoderBin::init(guint8 ch)
{
    gboolean ret = 0;
    gboolean crop_en = cmdArg.crop_en[ch/2];
    encData.ch = ch;
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d, crop %s", _FILE_, __LINE__, __FUNCTION__, ch, crop_en? "enable":"disable");

    re.bin = gst_bin_new(g_strdup_printf("encoderBin%d", ch));
    re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    //re.capsfilter2 = gst_element_factory_make("capsfilter2", "capsfilter");
    const gchar *encoder_factory =
        g_strcmp0(cmdArg.enc, ENC_H265) == 0 ? "vpuenc_hevc" : "vpuenc_h264";
    re.enc = gst_element_factory_make(encoder_factory, encoder_factory);
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    re.crop = gst_element_factory_make("videocrop", "crop");
    //re.overlay = gst_element_factory_make("textoverlay", "overlay");
    re.overlay = gst_element_factory_make("timeoverlay", "overlay");
    re.tee = gst_element_factory_make("tee", "tee");

    if (!re.bin || !re.queue || !re.enc || !re.rate || !re.convert || !re.capsfilter || !re.crop || !re.overlay || !re.tee) {
        __LOG(LOG_CRIT, "[GST][%s:%d] encoder element create error", _FILE_, __LINE__);
        return ret;
    }

#if 1
    //gst_bin_add_many(GST_BIN(re.bin), re.appsrc, re.sink, NULL);
    if (cmdArg.videorate_en)
        gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.crop, re.overlay, re.tee, NULL);
    else
        gst_bin_add_many(GST_BIN(re.bin), re.queue, re.convert, re.capsfilter, re.enc, re.crop, re.overlay, re.tee, NULL);
    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] encoder bin add err", _FILE_, __LINE__);
        return ret;
    }

#ifdef CHANNEL_EACH_CROP
    if(crop_en && cmdArg.overlay_en) {
        if (cmdArg.videorate_en)
            ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.tee, NULL);
        else
            ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.capsfilter, re.enc, re.tee, NULL);
    }
    else if(cmdArg.overlay_en) {
        if (cmdArg.videorate_en)
            ret = gst_element_link_many(re.queue, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.tee, NULL);
        else
            ret = gst_element_link_many(re.queue, re.overlay, re.convert, re.capsfilter, re.enc, re.tee, NULL);
    }
    else if(crop_en) {
        //ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.tee, NULL);
        if (cmdArg.videorate_en)
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.tee, NULL);
        else
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.capsfilter, re.enc, re.tee, NULL);
    }
    //else if(crop_en) ret = gst_element_link_many(re.queue, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
    else {
        if (cmdArg.videorate_en)
            ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, re.tee, NULL);
        else
            ret = gst_element_link_many(re.queue, re.capsfilter, re.enc, re.tee, NULL);
    }
    //if(cmdArg.mode) ret = gst_element_link_many(re.queue, re.crop, re.convert, re.enc, re.parse, re.queue2, NULL);
#else
    ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] encoder link err", _FILE_, __LINE__);
        return ret;
    }
#endif

    /* [채널 동기화 계측] 동일한 PTS 구간에서 branch, crop, videorate,
     * encoder 입출력과 record/RTSP 분기를 비교한다. 기본 비활성이며
     * 환경변수로만 설치된다. 모든 지점은 순서 의존 PTS hash와 요약 통계를
     * 남겨 프레임별 syslog가 측정에 주는 영향을 피한다. */
    install_channel_sync_trace(re.queue, "sink", ch, "branch_in", FALSE);
    if (crop_en)
        install_channel_sync_trace(re.crop, "src", ch, "crop_out", FALSE);
    if (cmdArg.videorate_en) {
        install_channel_sync_trace(re.rate, "sink", ch, "rate_in", FALSE);
        install_channel_sync_trace(re.rate, "src", ch, "rate_out", FALSE,
                                   TRUE);
        install_channel_rate_lineage(re.rate, ch);
    }
    install_channel_sync_trace(re.enc, "sink", ch, "enc_in", FALSE);
    install_channel_sync_trace(re.enc, "src", ch, "enc_out", FALSE);

    gint enc_fps = cmdArg.fps[STREAM_RTSP][ch];
    if (!cmdArg.dual_enc) {
        if (cmdArg.stream_en[STREAM_REC])
            enc_fps = cmdArg.fps[STREAM_REC][ch];
        else if (cmdArg.stream_en[STREAM_RTSP])
            enc_fps = cmdArg.fps[STREAM_RTSP][ch];
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw", 
                                        //"format", G_TYPE_STRING, "RGBx",
                                        //"width", G_TYPE_INT, cmdArg.width,
                                        //"height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, enc_fps, 1,
                                        NULL);

    g_object_set(re.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (ch % 2 == 0)
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
        //g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);
    else
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);
        //g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
        
    //if(cmdArg.rec_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rec_fps, "drop-only", TRUE, NULL);
    g_object_set(re.overlay, "valignment", 2, NULL);
    g_object_set(re.overlay, "halignment", 0, NULL);
    g_object_set(re.overlay, "font-desc", DEFAULT_OVERLAY_FONT, NULL);

    /* All encoder tuning now comes from edgeconf (VHL_CAM.i2cN.chX.*).
     * Defaults reproduce the previous behaviour: gop-size lands on 15 because
     * DEFAULT_GOP_SIZE is the follow-fps sentinel (0) and check_arg() resolves
     * it against the stream's fps (15 by default) — it is not a macro literal.
     * The rest match the plugin's own defaults. */
    g_object_set(re.enc, "bitrate", cmdArg.cam[ch].bps[STREAM_REC], NULL);
    g_object_set(re.enc, "gop-size", cmdArg.cam[ch].gop[STREAM_REC], NULL);
    /* quant is codec-specific; skip it cleanly when the selected encoder does
     * not expose the property. */
    enc_set_optional_int(re.enc, "quant", cmdArg.cam[ch].quant[STREAM_REC], ch);
    /* profile is installed on i.MX8MP only and qp-min/qp-max on i.MX8MP/i.MX8MM,
     * so set those through a guard instead of letting GLib warn per channel. */
    enc_set_optional_int(re.enc, "profile", cmdArg.cam[ch].profile[STREAM_REC], ch);
    enc_set_optional_int(re.enc, "qp-min", cmdArg.cam[ch].qp_min[STREAM_REC], ch);
    enc_set_optional_int(re.enc, "qp-max", cmdArg.cam[ch].qp_max[STREAM_REC], ch);

    //g_object_set(re.parse, "config-interval", -1, NULL);
    //g_object_set(re.rate, "max-rate", MAIN_FPS, "drop-only", FALSE, NULL);
    //g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.fps[STREAM_REC][ch], "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.fps[STREAM_RTSP][ch], "leaky", LEAKY_DOWNSTREAM, NULL);
    // [Queue 최적화] 시간 기반 고정 버퍼링 (JSON 설정값 사용)
    g_object_set(re.queue, "max-size-time", cmdArg.queue_enc_src_time_ms*GST_MSECOND, "max-size-buffers", 0, "leaky", LEAKY_DOWNSTREAM, NULL);
    /* [진단] 프레임 규격/실효 큐 깊이 1회 기록 + (설정 시) 프레임 수 기준 재사이징 */
    {
        GstPad *qsink = gst_element_get_static_pad(re.queue, "sink");
        if (qsink) {
            gst_pad_add_probe(qsink, GST_PAD_PROBE_TYPE_BUFFER,
                              (GstPadProbeCallback)enc_queue_caps_probe,
                              GINT_TO_POINTER((gint)ch), NULL);
            gst_object_unref(qsink);
        }
    }

    /* [무데이터 감시] 항상 설치한다. 프레임당 증가 연산 1회뿐이라 비용이 없고,
     * 소스가 죽어 파일이 안 생기는 기동 실패를 로그만으로 자기 진단하게 해준다. */
    {
        EncStat *st = &g_encStat[ch];
        st->queue = re.queue;
        st->active = TRUE;

        GstPad *p = gst_element_get_static_pad(re.queue, "sink");
        if (p) {
            gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                              (GstPadProbeCallback)enc_q_in_probe,
                              GINT_TO_POINTER((gint)ch), NULL);
            gst_object_unref(p);
        }

        static gboolean nodata_timer_installed = FALSE;
        if (!nodata_timer_installed) {
            nodata_timer_installed = TRUE;
            g_timeout_add_seconds(ENC_NODATA_WARN_SEC, enc_nodata_watch, NULL);
        }
    }

    /* [계측] enc_stat_sec > 0 일 때만 나머지 카운팅/시그널/리포트를 추가한다. */
    if (cmdArg.queue_enc_stat_sec > 0) {
        GstPad *p = gst_element_get_static_pad(re.queue, "src");
        if (p) {
            gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                              (GstPadProbeCallback)enc_q_out_probe,
                              GINT_TO_POINTER((gint)ch), NULL);
            gst_object_unref(p);
        }
        p = gst_element_get_static_pad(re.enc, "src");
        if (p) {
            gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER,
                              (GstPadProbeCallback)enc_out_probe,
                              GINT_TO_POINTER((gint)ch), NULL);
            gst_object_unref(p);
        }
        g_signal_connect(re.queue, "overrun", G_CALLBACK(enc_queue_overrun_cb),
                         GINT_TO_POINTER((gint)ch));

        static gboolean stat_timer_installed = FALSE;
        if (!stat_timer_installed) {
            stat_timer_installed = TRUE;
            g_timeout_add_seconds(cmdArg.queue_enc_stat_sec, enc_stat_report, NULL);
            __LOG(LOG_NOTICE, "[GST][%s:%d] enc-stat enabled (period %ds)",
                  _FILE_, __LINE__, cmdArg.queue_enc_stat_sec);
        }
    }
    /* videorate 없을 때 PTS 없는 버퍼가 mp4mux에 도달하면 크래시 발생.
     * enc src pad에서 PTS 없는 버퍼를 드롭하여 방지. */
    if (!cmdArg.videorate_en) {
        gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"),
                          GST_PAD_PROBE_TYPE_BUFFER,
                          (GstPadProbeCallback)drop_no_pts_probe, NULL, NULL);
    }

    if(cmdArg.levelMode == MODE_TEST)
    {
        gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        gst_pad_add_probe(gst_element_get_static_pad(re.queue, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.queue, NULL);
    }

#if 1
    sinkPad = gst_ghost_pad_new(g_strdup_printf("sink_pad_ch%d", ch), gst_element_get_static_pad(re.queue, "sink"));
    ret = gst_element_add_pad(re.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] encoder bin padd add err", _FILE_, __LINE__);
        return ret;
    }
#endif

    return ret;
}
