/*
 *
 * Cantops rtspServerBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "rtspServerBin.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gsth265parser.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/video/video.h>

static GstPadProbeReturn
drop_no_pts_probe_rtsp(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf && !GST_BUFFER_PTS_IS_VALID(buf)) {
        __LOG(LOG_INFO, "[GST][%s:%d] dropping buffer without PTS (rtsp enc src)", _FILE_, __LINE__);
        return GST_PAD_PROBE_DROP;
    }
    return GST_PAD_PROBE_OK;
}
// #include <rnnoise.h>
// static DenoiseState *den;

GstRTSPMountPoints *rtspMounts = NULL;
GstRTSPServer *rtspServer = NULL;
guint cleanSesson_id = 0;
guint removeSesson_id = 0;

/* 종료 시 appsrc에 EOS를 보내기 위한 전역 추적 배열 */
#define MAX_RTSP_APPSRC (MAX_CHANNEL + 1)
static GstElement *g_rtsp_appsrc[MAX_RTSP_APPSRC] = { NULL };
static guint8 g_rtsp_appsrc_count = 0;
/* 배열·count 보호 — media_configure/unprepared(RTSP 스레드)와 종료 경로 공유.
 * 정적 할당 GMutex는 별도 init 불필요 */
static GMutex g_rtsp_appsrc_lock;

#define RTSP_SYNC_FNV_OFFSET G_GUINT64_CONSTANT(1469598103934665603)
#define RTSP_SYNC_FNV_PRIME G_GUINT64_CONSTANT(1099511628211)
#define RTSP_VIDEO_CLOCK_RATE 90000
#define RTSP_FRAME_ID_SEI_PAYLOAD_SIZE 28
#define RTSP_FRAME_ID_SEI_COUNTRY_CODE 0xff
#define RTSP_FRAME_ID_SEI_COUNTRY_EXTENSION 0xc1

static const guint8 rtsp_frame_id_sei_magic[8] = {
  'G', 'S', 'T', 'S', 'Y', 'N', 'C', '1'
};

typedef struct {
  gboolean started;
  gboolean complete;
  guint64 frames;
  guint64 invalid_pts;
  guint64 pts_backwards;
  guint64 missing_origin_meta;
  guint64 pts_hash;
  guint64 normalized_pts_hash;
  guint64 origin_pts_hash;
  guint64 origin_index_hash;
  GstClockTime first_pts;
  GstClockTime previous_pts;
  GstClockTime last_pts;
  GstClockTime first_origin_pts;
  GstClockTime last_origin_pts;
  GstClockTime delta_min;
  GstClockTime delta_max;
  guint64 delta_sum;
} RtspSyncBufferStage;

typedef struct {
  guint8 ch;
  guint duration_sec;
  GstCaps *reference;
  GMutex lock;
  gboolean active;
  gboolean bridge_started;
  gboolean bridge_complete;
  guint64 bridge_frames;
  guint64 bridge_invalid_pts;
  guint64 bridge_no_appsrc;
  guint64 bridge_full_drop;
  guint64 bridge_key_wait_drop;
  guint64 factory_queue_overrun;
  guint64 bridge_interrupted_drop;
  guint64 bridge_copy_failure;
  guint64 bridge_stripped_pts_valid;
  guint64 bridge_meta_failure;
  guint64 push_attempt;
  guint64 push_ok;
  guint64 push_failure;
  guint64 frame_id_sei_inserted;
  guint64 frame_id_sei_failed;
  guint64 frame_id_sei_time_count;
  guint64 frame_id_sei_time_sum_ns;
  GstClockTime frame_id_sei_time_min_ns;
  GstClockTime frame_id_sei_time_max_ns;
  guint64 frame_id_sei_time_buckets[9];
  guint64 upstream_pts_hash;
  GstClockTime bridge_first_pts;
  GstClockTime bridge_previous_pts;
  GstClockTime bridge_last_pts;
  guint64 bridge_pts_backwards;
  RtspSyncBufferStage appsrc_stage;
  RtspSyncBufferStage pay_stage;
  gboolean rtp_started;
  gboolean rtp_complete;
  guint64 rtp_packets;
  guint64 rtp_markers;
  guint64 rtp_timestamps;
  guint64 rtp_timestamp_backwards;
  guint64 rtp_timestamp_hash;
  guint64 rtp_delta_sum;
  guint64 rtp_delta_min;
  guint64 rtp_delta_max;
  guint32 rtp_first_raw;
  guint32 rtp_previous_raw;
  guint32 rtp_last_raw;
  guint64 rtp_wrap_base;
  guint64 rtp_first_unwrapped;
  guint64 rtp_previous_unwrapped;
  guint64 rtp_last_unwrapped;
} RtspSyncTrace;

typedef enum {
  RTSP_SYNC_STAGE_APPSRC,
  RTSP_SYNC_STAGE_PAY
} RtspSyncStageKind;

typedef struct {
  RtspSyncTrace *trace;
  RtspSyncStageKind kind;
} RtspSyncProbeCtx;

typedef struct {
  guint8 ch;
  guint after_sec;
  guint duration_sec;
  gint64 first_buffer_us;
  gboolean triggered;
} RtspTestStallCtx;

static gboolean rtsp_test_stall_config(guint8 ch, guint *after_sec,
                                       guint *duration_sec) {
  if (cmdArg.rtsp_test_stall_ch != ch ||
      cmdArg.rtsp_test_stall_duration_sec <= 0)
    return FALSE;

  *after_sec = (guint)cmdArg.rtsp_test_stall_after_sec;
  *duration_sec = (guint)cmdArg.rtsp_test_stall_duration_sec;
  return TRUE;
}

static GstPadProbeReturn rtsp_test_stall_probe(GstPad *pad,
                                               GstPadProbeInfo *probe_info,
                                               gpointer user_data) {
  (void)pad;
  RtspTestStallCtx *ctx = (RtspTestStallCtx *)user_data;
  if (!(GST_PAD_PROBE_INFO_TYPE(probe_info) &
        (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST)))
    return GST_PAD_PROBE_OK;

  gint64 now_us = g_get_monotonic_time();
  if (ctx->first_buffer_us == 0)
    ctx->first_buffer_us = now_us;

  if (!ctx->triggered &&
      now_us - ctx->first_buffer_us >=
          (gint64)ctx->after_sec * G_USEC_PER_SEC) {
    ctx->triggered = TRUE;
    __LOG(LOG_WARNING,
          "[RTSP_TEST_STALL][%s:%d] ch=%u start duration_sec=%u mono_ns=%" G_GINT64_FORMAT,
          _FILE_, __LINE__, ctx->ch, ctx->duration_sec,
          g_get_monotonic_time() * 1000);
    g_usleep((gulong)ctx->duration_sec * G_USEC_PER_SEC);
    __LOG(LOG_WARNING, "[RTSP_TEST_STALL][%s:%d] ch=%u end mono_ns=%" G_GINT64_FORMAT,
          _FILE_, __LINE__, ctx->ch, g_get_monotonic_time() * 1000);
  }

  return GST_PAD_PROBE_OK;
}

static guint64 rtsp_sync_hash_u64(guint64 hash, guint64 value) {
  for (guint i = 0; i < sizeof(value); i++) {
    hash ^= (value >> (i * 8)) & 0xff;
    hash *= RTSP_SYNC_FNV_PRIME;
  }
  return hash;
}

static guint rtsp_sync_trace_duration() {
  return cmdArg.rtsp_sync_trace_sec > 0
             ? (guint)cmdArg.rtsp_sync_trace_sec
             : 0;
}

static gboolean rtsp_frame_id_sei_enabled() {
  return cmdArg.rtsp_frame_id_sei;
}

static GstBuffer *rtsp_frame_id_sei_insert(RtspServerData *info,
                                           GstBuffer *buffer,
                                           GstClockTime origin_pts) {
  if (!info || !buffer || !GST_CLOCK_TIME_IS_VALID(origin_pts) ||
      g_strcmp0(cmdArg.enc, ENC_H265) != 0)
    return NULL;

  GstH265Parser *parser = (GstH265Parser *)info->frame_id_parser;
  if (!parser) {
    parser = gst_h265_parser_new();
    if (!parser)
      return NULL;
    info->frame_id_parser = parser;
  }

  guint8 payload[RTSP_FRAME_ID_SEI_PAYLOAD_SIZE] = { 0 };
  memcpy(payload, rtsp_frame_id_sei_magic,
         sizeof(rtsp_frame_id_sei_magic));
  payload[8] = 1;
  payload[9] = info->ch;
  guint fps = cmdArg.fps[STREAM_RTSP][info->ch];
  guint64 frame_id = fps > 0
                         ? gst_util_uint64_scale_round(
                               origin_pts, fps, GST_SECOND)
                         : 0;
  GST_WRITE_UINT64_BE(payload + 12, frame_id);
  GST_WRITE_UINT64_BE(payload + 20, origin_pts);

  GstH265SEIMessage message = {};
  message.payloadType = GST_H265_SEI_REGISTERED_USER_DATA;
  message.payload.registered_user_data.country_code =
      RTSP_FRAME_ID_SEI_COUNTRY_CODE;
  message.payload.registered_user_data.country_code_extension =
      RTSP_FRAME_ID_SEI_COUNTRY_EXTENSION;
  message.payload.registered_user_data.data = payload;
  message.payload.registered_user_data.size = sizeof(payload);

  GArray *messages = g_array_sized_new(
      FALSE, FALSE, sizeof(GstH265SEIMessage), 1);
  g_array_append_val(messages, message);
  GstMemory *sei = gst_h265_create_sei_memory(0, 1, 4, messages);
  g_array_free(messages, TRUE);
  if (!sei)
    return NULL;

  GstBuffer *result = gst_h265_parser_insert_sei(parser, buffer, sei);
  gst_memory_unref(sei);
  return result;
}

static RtspSyncTrace *rtsp_sync_trace_new(guint8 ch) {
  guint duration_sec = rtsp_sync_trace_duration();
  if (duration_sec == 0)
    return NULL;

  RtspSyncTrace *trace = g_new0(RtspSyncTrace, 1);
  trace->ch = ch;
  trace->duration_sec = duration_sec;
  trace->reference = gst_caps_new_simple(
      "timestamp/x-gstapp-rtsp-origin", "channel", G_TYPE_INT, (gint)ch,
      NULL);
  trace->upstream_pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->appsrc_stage.pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->appsrc_stage.normalized_pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->appsrc_stage.origin_pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->appsrc_stage.origin_index_hash = RTSP_SYNC_FNV_OFFSET;
  trace->appsrc_stage.delta_min = GST_CLOCK_TIME_NONE;
  trace->pay_stage.pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->pay_stage.normalized_pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->pay_stage.origin_pts_hash = RTSP_SYNC_FNV_OFFSET;
  trace->pay_stage.origin_index_hash = RTSP_SYNC_FNV_OFFSET;
  trace->pay_stage.delta_min = GST_CLOCK_TIME_NONE;
  trace->rtp_timestamp_hash = RTSP_SYNC_FNV_OFFSET;
  trace->rtp_delta_min = G_MAXUINT64;
  trace->frame_id_sei_time_min_ns = GST_CLOCK_TIME_NONE;
  g_mutex_init(&trace->lock);

  __LOG(LOG_NOTICE,
        "[RTSP_SYNC][%s:%d] ch=%u enabled duration_sec=%u",
        _FILE_, __LINE__, ch, duration_sec);
  return trace;
}

static void rtsp_sync_trace_free(RtspSyncTrace *trace) {
  if (!trace)
    return;
  if (trace->reference)
    gst_caps_unref(trace->reference);
  g_mutex_clear(&trace->lock);
  g_free(trace);
}

static gboolean rtsp_sync_bridge_input(RtspSyncTrace *trace,
                                       GstBuffer *buffer,
                                       gboolean has_appsrc,
                                       gboolean drop_full,
                                       gboolean drop_key_wait,
                                       gboolean interrupted,
                                       guint64 *origin_index) {
  if (!trace || !buffer)
    return FALSE;

  GstClockTime pts = GST_BUFFER_PTS(buffer);
  gboolean trace_buffer = FALSE;

  g_mutex_lock(&trace->lock);
  if (!trace->active || trace->bridge_complete)
    goto bridge_input_out;

  if (!GST_CLOCK_TIME_IS_VALID(pts)) {
    trace->bridge_invalid_pts++;
    goto bridge_input_out;
  }

  if (!trace->bridge_started) {
    trace->bridge_started = TRUE;
    trace->bridge_first_pts = pts;
  } else if (pts >= trace->bridge_first_pts &&
             pts - trace->bridge_first_pts >=
                 (GstClockTime)trace->duration_sec * GST_SECOND) {
    GstClockTime span = trace->bridge_last_pts - trace->bridge_first_pts;
    trace->bridge_complete = TRUE;
    __LOG(LOG_NOTICE,
          "[RTSP_SYNC][%s:%d] ch=%u stage=bridge summary"
          " duration_pts_ns=%" G_GUINT64_FORMAT " frames=%"
          G_GUINT64_FORMAT " invalid_pts=%" G_GUINT64_FORMAT
          " pts_backwards=%" G_GUINT64_FORMAT " first_pts_ns=%"
          G_GUINT64_FORMAT " last_pts_ns=%" G_GUINT64_FORMAT
          " upstream_pts_hash=%016" G_GINT64_MODIFIER
          "x no_appsrc=%" G_GUINT64_FORMAT " full_drop=%"
          G_GUINT64_FORMAT " key_wait_drop=%" G_GUINT64_FORMAT
          " factory_queue_overrun=%" G_GUINT64_FORMAT
          " interrupted_drop=%" G_GUINT64_FORMAT " copy_fail=%"
          G_GUINT64_FORMAT " stripped_pts_valid=%" G_GUINT64_FORMAT
          " meta_fail=%" G_GUINT64_FORMAT " push_attempt=%"
          G_GUINT64_FORMAT " push_ok=%" G_GUINT64_FORMAT
          " push_fail=%" G_GUINT64_FORMAT " frame_id_sei_inserted=%"
          G_GUINT64_FORMAT " frame_id_sei_failed=%" G_GUINT64_FORMAT,
          _FILE_, __LINE__, trace->ch, span, trace->bridge_frames,
          trace->bridge_invalid_pts, trace->bridge_pts_backwards,
          trace->bridge_first_pts, trace->bridge_last_pts,
          trace->upstream_pts_hash, trace->bridge_no_appsrc,
          trace->bridge_full_drop, trace->bridge_key_wait_drop,
          trace->factory_queue_overrun,
          trace->bridge_interrupted_drop, trace->bridge_copy_failure,
          trace->bridge_stripped_pts_valid, trace->bridge_meta_failure,
          trace->push_attempt, trace->push_ok, trace->push_failure,
          trace->frame_id_sei_inserted, trace->frame_id_sei_failed);
    if (trace->frame_id_sei_time_count > 0) {
      __LOG(LOG_NOTICE,
            "[RTSP_FRAME_ID][%s:%d] ch=%u timing summary count=%"
            G_GUINT64_FORMAT " avg_ns=%" G_GUINT64_FORMAT " min_ns=%"
            G_GUINT64_FORMAT " max_ns=%" G_GUINT64_FORMAT
            " b_0_5us=%" G_GUINT64_FORMAT " b_5_10us=%"
            G_GUINT64_FORMAT " b_10_25us=%" G_GUINT64_FORMAT
            " b_25_50us=%" G_GUINT64_FORMAT " b_50_100us=%"
            G_GUINT64_FORMAT " b_100_250us=%" G_GUINT64_FORMAT
            " b_250_500us=%" G_GUINT64_FORMAT " b_500_1000us=%"
            G_GUINT64_FORMAT " b_gt_1000us=%" G_GUINT64_FORMAT,
            _FILE_, __LINE__, trace->ch, trace->frame_id_sei_time_count,
            trace->frame_id_sei_time_sum_ns /
                trace->frame_id_sei_time_count,
            trace->frame_id_sei_time_min_ns,
            trace->frame_id_sei_time_max_ns,
            trace->frame_id_sei_time_buckets[0],
            trace->frame_id_sei_time_buckets[1],
            trace->frame_id_sei_time_buckets[2],
            trace->frame_id_sei_time_buckets[3],
            trace->frame_id_sei_time_buckets[4],
            trace->frame_id_sei_time_buckets[5],
            trace->frame_id_sei_time_buckets[6],
            trace->frame_id_sei_time_buckets[7],
            trace->frame_id_sei_time_buckets[8]);
    }
    goto bridge_input_out;
  }

  if (trace->bridge_frames > 0 && pts < trace->bridge_previous_pts)
    trace->bridge_pts_backwards++;
  trace->bridge_frames++;
  trace->bridge_previous_pts = pts;
  trace->bridge_last_pts = pts;
  trace->upstream_pts_hash =
      rtsp_sync_hash_u64(trace->upstream_pts_hash, pts);
  *origin_index = trace->bridge_frames;
  trace_buffer = TRUE;

  if (!has_appsrc)
    trace->bridge_no_appsrc++;
  else if (drop_full)
    trace->bridge_full_drop++;
  else if (drop_key_wait)
    trace->bridge_key_wait_drop++;
  else if (interrupted)
    trace->bridge_interrupted_drop++;

bridge_input_out:
  g_mutex_unlock(&trace->lock);
  return trace_buffer;
}

static void rtsp_sync_record_copy(RtspSyncTrace *trace,
                                  gboolean trace_buffer,
                                  gboolean copy_ok,
                                  gboolean stripped_pts_valid,
                                  gboolean meta_ok) {
  if (!trace || !trace_buffer)
    return;

  g_mutex_lock(&trace->lock);
  if (!copy_ok)
    trace->bridge_copy_failure++;
  if (stripped_pts_valid)
    trace->bridge_stripped_pts_valid++;
  if (copy_ok && !meta_ok)
    trace->bridge_meta_failure++;
  g_mutex_unlock(&trace->lock);
}

static void rtsp_sync_record_push(RtspSyncTrace *trace,
                                  gboolean trace_buffer,
                                  GstFlowReturn result) {
  if (!trace || !trace_buffer)
    return;

  g_mutex_lock(&trace->lock);
  trace->push_attempt++;
  if (result == GST_FLOW_OK)
    trace->push_ok++;
  else
    trace->push_failure++;
  g_mutex_unlock(&trace->lock);
}

static void rtsp_sync_record_frame_id_sei(RtspSyncTrace *trace,
                                          gboolean trace_buffer,
                                          gboolean inserted,
                                          GstClockTime elapsed_ns) {
  if (!trace || !trace_buffer)
    return;

  g_mutex_lock(&trace->lock);
  if (inserted)
    trace->frame_id_sei_inserted++;
  else
    trace->frame_id_sei_failed++;
  if (GST_CLOCK_TIME_IS_VALID(elapsed_ns)) {
    static const GstClockTime upper_ns[] = {
      5 * GST_USECOND, 10 * GST_USECOND, 25 * GST_USECOND,
      50 * GST_USECOND, 100 * GST_USECOND, 250 * GST_USECOND,
      500 * GST_USECOND, GST_MSECOND, GST_CLOCK_TIME_NONE
    };

    trace->frame_id_sei_time_count++;
    trace->frame_id_sei_time_sum_ns += elapsed_ns;
    if (!GST_CLOCK_TIME_IS_VALID(trace->frame_id_sei_time_min_ns) ||
        elapsed_ns < trace->frame_id_sei_time_min_ns)
      trace->frame_id_sei_time_min_ns = elapsed_ns;
    if (elapsed_ns > trace->frame_id_sei_time_max_ns)
      trace->frame_id_sei_time_max_ns = elapsed_ns;

    for (guint i = 0; i < G_N_ELEMENTS(upper_ns); i++) {
      if (!GST_CLOCK_TIME_IS_VALID(upper_ns[i]) || elapsed_ns <= upper_ns[i]) {
        trace->frame_id_sei_time_buckets[i]++;
        break;
      }
    }
  }
  g_mutex_unlock(&trace->lock);
}

static GstPadProbeReturn rtsp_sync_buffer_probe(GstPad *pad,
                                                GstPadProbeInfo *probe_info,
                                                gpointer user_data) {
  RtspSyncProbeCtx *ctx = (RtspSyncProbeCtx *)user_data;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(probe_info);
  if (!ctx || !ctx->trace || !buffer)
    return GST_PAD_PROBE_OK;

  RtspSyncTrace *trace = ctx->trace;
  GstClockTime pts = GST_BUFFER_PTS(buffer);
  GstReferenceTimestampMeta *meta = gst_buffer_get_reference_timestamp_meta(
      buffer, trace->reference);

  g_mutex_lock(&trace->lock);
  if (!trace->active) {
    g_mutex_unlock(&trace->lock);
    return GST_PAD_PROBE_OK;
  }

  RtspSyncBufferStage *stage =
      ctx->kind == RTSP_SYNC_STAGE_APPSRC ? &trace->appsrc_stage
                                         : &trace->pay_stage;
  const gchar *stage_name =
      ctx->kind == RTSP_SYNC_STAGE_APPSRC ? "appsrc_out" : "pay_in";

  if (stage->complete) {
    g_mutex_unlock(&trace->lock);
    return GST_PAD_PROBE_OK;
  }
  if (!GST_CLOCK_TIME_IS_VALID(pts)) {
    stage->invalid_pts++;
    g_mutex_unlock(&trace->lock);
    return GST_PAD_PROBE_OK;
  }

  if (!stage->started) {
    stage->started = TRUE;
    stage->first_pts = pts;
  } else if (pts >= stage->first_pts &&
             pts - stage->first_pts >=
                 (GstClockTime)trace->duration_sec * GST_SECOND) {
    GstClockTime span = stage->last_pts - stage->first_pts;
    guint64 delta_count = stage->frames > 0 ? stage->frames - 1 : 0;
    guint64 delta_avg =
        delta_count > 0 ? stage->delta_sum / delta_count : 0;
    stage->complete = TRUE;
    __LOG(LOG_NOTICE,
          "[RTSP_SYNC][%s:%d] ch=%u stage=%s summary"
          " duration_pts_ns=%" G_GUINT64_FORMAT " frames=%"
          G_GUINT64_FORMAT " invalid_pts=%" G_GUINT64_FORMAT
          " pts_backwards=%" G_GUINT64_FORMAT " first_pts_ns=%"
          G_GUINT64_FORMAT " last_pts_ns=%" G_GUINT64_FORMAT
          " pts_hash=%016" G_GINT64_MODIFIER
          "x normalized_pts_hash=%016" G_GINT64_MODIFIER
          "x delta_min_ns=%" G_GUINT64_FORMAT " delta_max_ns=%"
          G_GUINT64_FORMAT " delta_avg_ns=%" G_GUINT64_FORMAT
          " missing_origin_meta=%" G_GUINT64_FORMAT
          " first_origin_pts_ns=%" G_GUINT64_FORMAT
          " last_origin_pts_ns=%" G_GUINT64_FORMAT
          " origin_pts_hash=%016" G_GINT64_MODIFIER
          "x origin_index_hash=%016" G_GINT64_MODIFIER "x",
          _FILE_, __LINE__, trace->ch, stage_name, span, stage->frames,
          stage->invalid_pts, stage->pts_backwards, stage->first_pts,
          stage->last_pts, stage->pts_hash, stage->normalized_pts_hash,
          stage->delta_min == GST_CLOCK_TIME_NONE ? 0 : stage->delta_min,
          stage->delta_max, delta_avg, stage->missing_origin_meta,
          stage->first_origin_pts, stage->last_origin_pts,
          stage->origin_pts_hash, stage->origin_index_hash);
    g_mutex_unlock(&trace->lock);
    return GST_PAD_PROBE_OK;
  }

  if (stage->frames > 0) {
    if (pts < stage->previous_pts) {
      stage->pts_backwards++;
    } else {
      GstClockTime delta = pts - stage->previous_pts;
      if (stage->delta_min == GST_CLOCK_TIME_NONE ||
          delta < stage->delta_min)
        stage->delta_min = delta;
      if (delta > stage->delta_max)
        stage->delta_max = delta;
      stage->delta_sum += delta;
    }
  }

  stage->frames++;
  stage->previous_pts = pts;
  stage->last_pts = pts;
  stage->pts_hash = rtsp_sync_hash_u64(stage->pts_hash, pts);
  stage->normalized_pts_hash =
      rtsp_sync_hash_u64(stage->normalized_pts_hash, pts - stage->first_pts);

  if (!meta) {
    stage->missing_origin_meta++;
  } else {
    if (stage->frames == 1)
      stage->first_origin_pts = meta->timestamp;
    stage->last_origin_pts = meta->timestamp;
    stage->origin_pts_hash =
        rtsp_sync_hash_u64(stage->origin_pts_hash, meta->timestamp);
    stage->origin_index_hash =
        rtsp_sync_hash_u64(stage->origin_index_hash, meta->duration);
  }

  g_mutex_unlock(&trace->lock);
  return GST_PAD_PROBE_OK;
}

static void rtsp_sync_process_rtp_buffer(RtspSyncTrace *trace,
                                         GstBuffer *buffer) {
  guint8 header[12];
  if (!trace || !buffer ||
      gst_buffer_extract(buffer, 0, header, sizeof(header)) != sizeof(header) ||
      (header[0] >> 6) != 2)
    return;

  gboolean marker = (header[1] & 0x80) != 0;
  guint32 timestamp = ((guint32)header[4] << 24) |
                      ((guint32)header[5] << 16) |
                      ((guint32)header[6] << 8) | (guint32)header[7];

  g_mutex_lock(&trace->lock);
  if (!trace->active || trace->rtp_complete) {
    g_mutex_unlock(&trace->lock);
    return;
  }

  guint64 unwrapped;
  gboolean new_timestamp = !trace->rtp_started ||
                           timestamp != trace->rtp_previous_raw;
  if (!trace->rtp_started) {
    trace->rtp_started = TRUE;
    trace->rtp_first_raw = timestamp;
    trace->rtp_first_unwrapped = timestamp;
    trace->rtp_previous_unwrapped = timestamp;
    trace->rtp_delta_min = G_MAXUINT64;
  }

  if (timestamp < trace->rtp_previous_raw &&
      trace->rtp_previous_raw - timestamp > G_MAXUINT32 / 2)
    trace->rtp_wrap_base += G_GUINT64_CONSTANT(1) << 32;
  unwrapped = trace->rtp_wrap_base + timestamp;

  if (new_timestamp && trace->rtp_timestamps > 0 &&
      unwrapped < trace->rtp_previous_unwrapped) {
    trace->rtp_timestamp_backwards++;
  }

  if (new_timestamp &&
      unwrapped >= trace->rtp_first_unwrapped &&
      unwrapped - trace->rtp_first_unwrapped >=
          (guint64)trace->duration_sec * RTSP_VIDEO_CLOCK_RATE) {
    guint64 delta_count =
        trace->rtp_timestamps > 0 ? trace->rtp_timestamps - 1 : 0;
    guint64 delta_avg =
        delta_count > 0 ? trace->rtp_delta_sum / delta_count : 0;
    trace->rtp_complete = TRUE;
    __LOG(LOG_NOTICE,
          "[RTSP_SYNC][%s:%d] ch=%u stage=rtp_out summary packets=%"
          G_GUINT64_FORMAT " marker_frames=%" G_GUINT64_FORMAT
          " distinct_timestamps=%" G_GUINT64_FORMAT
          " timestamp_backwards=%" G_GUINT64_FORMAT
          " first_timestamp=%u last_timestamp=%u span_ticks=%"
          G_GUINT64_FORMAT " normalized_timestamp_hash=%016"
          G_GINT64_MODIFIER "x delta_min_ticks=%" G_GUINT64_FORMAT
          " delta_max_ticks=%" G_GUINT64_FORMAT " delta_avg_ticks=%"
          G_GUINT64_FORMAT,
          _FILE_, __LINE__, trace->ch, trace->rtp_packets,
          trace->rtp_markers, trace->rtp_timestamps,
          trace->rtp_timestamp_backwards, trace->rtp_first_raw,
          trace->rtp_last_raw,
          trace->rtp_last_unwrapped - trace->rtp_first_unwrapped,
          trace->rtp_timestamp_hash,
          trace->rtp_delta_min == G_MAXUINT64 ? 0 : trace->rtp_delta_min,
          trace->rtp_delta_max, delta_avg);
    g_mutex_unlock(&trace->lock);
    return;
  }

  trace->rtp_packets++;
  if (marker)
    trace->rtp_markers++;
  if (new_timestamp) {
    if (trace->rtp_timestamps > 0 &&
        unwrapped >= trace->rtp_previous_unwrapped) {
      guint64 delta = unwrapped - trace->rtp_previous_unwrapped;
      if (delta < trace->rtp_delta_min)
        trace->rtp_delta_min = delta;
      if (delta > trace->rtp_delta_max)
        trace->rtp_delta_max = delta;
      trace->rtp_delta_sum += delta;
    }
    trace->rtp_timestamps++;
    trace->rtp_last_raw = timestamp;
    trace->rtp_last_unwrapped = unwrapped;
    trace->rtp_timestamp_hash = rtsp_sync_hash_u64(
        trace->rtp_timestamp_hash, unwrapped - trace->rtp_first_unwrapped);
    trace->rtp_previous_raw = timestamp;
    trace->rtp_previous_unwrapped = unwrapped;
  }

  g_mutex_unlock(&trace->lock);
}

static GstPadProbeReturn rtsp_sync_rtp_probe(GstPad *pad,
                                             GstPadProbeInfo *probe_info,
                                             gpointer user_data) {
  RtspSyncTrace *trace = (RtspSyncTrace *)user_data;
  if (GST_PAD_PROBE_INFO_TYPE(probe_info) & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(probe_info);
    rtsp_sync_process_rtp_buffer(trace, buffer);
    return GST_PAD_PROBE_OK;
  }

  if (GST_PAD_PROBE_INFO_TYPE(probe_info) &
      GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    GstBufferList *list = GST_PAD_PROBE_INFO_BUFFER_LIST(probe_info);
    guint length = gst_buffer_list_length(list);
    for (guint i = 0; i < length; i++)
      rtsp_sync_process_rtp_buffer(trace, gst_buffer_list_get(list, i));
  }

  return GST_PAD_PROBE_OK;
}

static void rtsp_sync_install_media_probes(RtspServerData *info,
                                           GstElement *element,
                                           GstElement *appsrc) {
  RtspSyncTrace *trace = (RtspSyncTrace *)info->sync_trace;
  guint stall_after_sec = 0;
  guint stall_duration_sec = 0;
  gboolean stall_matches = rtsp_test_stall_config(
      info->ch, &stall_after_sec, &stall_duration_sec);
  if (!trace && !stall_matches)
    return;

  GstPad *pad = NULL;
  if (trace) {
    pad = gst_element_get_static_pad(appsrc, "src");
    if (pad) {
      RtspSyncProbeCtx *ctx = g_new0(RtspSyncProbeCtx, 1);
      ctx->trace = trace;
      ctx->kind = RTSP_SYNC_STAGE_APPSRC;
      gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                        rtsp_sync_buffer_probe, ctx, g_free);
      gst_object_unref(pad);
    }
  }

  GstElement *pay =
      gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay0");
  if (!pay) {
    __LOG(LOG_ERR, "[RTSP_SYNC][%s:%d] ch=%u pay0 not found",
          _FILE_, __LINE__, info->ch);
    return;
  }

  if (trace) {
    pad = gst_element_get_static_pad(pay, "sink");
    if (pad) {
      RtspSyncProbeCtx *ctx = g_new0(RtspSyncProbeCtx, 1);
      ctx->trace = trace;
      ctx->kind = RTSP_SYNC_STAGE_PAY;
      gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                        rtsp_sync_buffer_probe, ctx, g_free);
      gst_object_unref(pad);
    }
  }

  pad = gst_element_get_static_pad(pay, "src");
  if (pad) {
    if (stall_matches) {
      RtspTestStallCtx *stall_ctx = g_new0(RtspTestStallCtx, 1);
      stall_ctx->ch = info->ch;
      stall_ctx->after_sec = stall_after_sec;
      stall_ctx->duration_sec = stall_duration_sec;
      gst_pad_add_probe(pad,
                        (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER |
                                          GST_PAD_PROBE_TYPE_BUFFER_LIST),
                        rtsp_test_stall_probe, stall_ctx, g_free);
    }
    if (trace) {
      gst_pad_add_probe(pad,
                        (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER |
                                          GST_PAD_PROBE_TYPE_BUFFER_LIST),
                        rtsp_sync_rtp_probe, trace, NULL);
    }
    gst_object_unref(pad);
  }
  gst_object_unref(pay);
}

static void rtsp_sync_activate(RtspServerData *info) {
  RtspSyncTrace *trace = (RtspSyncTrace *)info->sync_trace;
  if (!trace)
    return;

  g_mutex_lock(&trace->lock);
  trace->active = TRUE;
  g_mutex_unlock(&trace->lock);
}

static gint clamp_int(gint value, gint min_value, gint max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

typedef struct _RtspPayProbeCtx {
  RtspServerData *info;
  GstElement *media_element;
  gint64 last_log_us;
} RtspPayProbeCtx;

static void rtsp_pay_probe_ctx_free(gpointer data) {
  RtspPayProbeCtx *ctx = (RtspPayProbeCtx *)data;
  if (!ctx)
    return;

  if (ctx->media_element)
    gst_object_unref(ctx->media_element);

  g_free(ctx);
}

static GstPadProbeReturn rtsp_pay_src_probe(GstPad *pad,
                                            GstPadProbeInfo *probe_info,
                                            gpointer user_data) {
  RtspPayProbeCtx *ctx = (RtspPayProbeCtx *)user_data;
  if (!ctx || !ctx->info || !ctx->media_element)
    return GST_PAD_PROBE_OK;

  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(probe_info);
  if (!buffer)
    return GST_PAD_PROBE_OK;

  GstClockTime pts = GST_BUFFER_PTS(buffer);
  if (!GST_CLOCK_TIME_IS_VALID(pts))
    return GST_PAD_PROBE_OK;

  GstClock *clock = gst_element_get_clock(ctx->media_element);
  if (!clock)
    return GST_PAD_PROBE_OK;

  GstClockTime now = gst_clock_get_time(clock);
  gst_object_unref(clock);

  GstClockTime base = gst_element_get_base_time(ctx->media_element);
  GstClockTime running = (now > base) ? (now - base) : 0;
  GstClockTime rtsp_latency = (running >= pts) ? (running - pts) : 0;

  gint64 now_us = g_get_monotonic_time();
  if (ctx->last_log_us == 0 || (now_us - ctx->last_log_us) >= 1000000) {
    ctx->last_log_us = now_us;
    __LOG(LOG_NOTICE,
          "[RTSP][%s:%d] ch%d rtsp-out-latency=%" GST_TIME_FORMAT
          " pts=%" GST_TIME_FORMAT,
          _FILE_, __LINE__, ctx->info->ch, GST_TIME_ARGS(rtsp_latency),
          GST_TIME_ARGS(pts));
  }

  return GST_PAD_PROBE_OK;
}

static void enough_data(GstElement *source, gpointer user_data) {
  RtspServerData *info = (RtspServerData *)user_data;

  g_atomic_int_set(&info->appsrc_full, 1);
  /* push 스레드에서만 emit되므로 스로틀 타임스탬프는 락 불필요 */
  gint64 now_us = g_get_monotonic_time();
  if (info->last_enough_log_us == 0 ||
      (now_us - info->last_enough_log_us) >= 1000000) {
    info->last_enough_log_us = now_us;
    __LOG(LOG_WARNING,
          "[RTSP][%s:%d] ch %d appsrc full — dropping until need-data",
          _FILE_, __LINE__, info->ch);
  }
}

/* 강제 IDR 요청 최소 간격 — 혼잡(TCP 백프레셔) 시 드롭 사이클마다 대형
 * IDR이 반복 투입되어 혼잡을 악화시키는 되먹임을 차단한다 */
#define RTSP_KICK_MIN_INTERVAL_US G_USEC_PER_SEC

static void request_keyframe(RtspServerData *info) {
  if (info->kick_sink == NULL)
    return;
  gint64 now_us = g_get_monotonic_time();
  g_mutex_lock(&info->lock);
  if (info->last_kick_us != 0 &&
      (now_us - info->last_kick_us) < RTSP_KICK_MIN_INTERVAL_US) {
    g_mutex_unlock(&info->lock);
    return;
  }
  info->last_kick_us = now_us;
  g_mutex_unlock(&info->lock);
  /* appsink에 upstream force-key-unit을 보내면 tee를 거슬러 vpuenc까지
   * 전달된다 — 접속 직후/드롭 재개 시 IDR 대기(GOP 위상)를 단축 */
  GstEvent *event =
      gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0);
  if (!gst_element_send_event(info->kick_sink, event))
    __LOG(LOG_INFO, "[RTSP][%s:%d] ch%d force-keyunit send failed", _FILE_,
          __LINE__, info->ch);
}

static void need_data(GstElement *source, guint length, gpointer user_data) {
  RtspServerData *info = (RtspServerData *)user_data;
  gboolean want_key;

  g_atomic_int_set(&info->appsrc_full, 0);
  /* 드롭 구간 종료 — 키프레임 대기 중이면 즉시 IDR을 요청해 재개 지연 제거 */
  g_mutex_lock(&info->lock);
  want_key = info->wait_keyframe;
  g_mutex_unlock(&info->lock);
  if (want_key)
    request_keyframe(info);
}

static void rtsp_factory_queue_overrun(GstElement *queue,
                                       gpointer user_data) {
  (void)queue;
  RtspServerData *info = (RtspServerData *)user_data;
  RtspSyncTrace *trace = info ? (RtspSyncTrace *)info->sync_trace : NULL;
  if (!trace)
    return;

  g_mutex_lock(&trace->lock);
  if (trace->active && !trace->bridge_complete)
    trace->factory_queue_overrun++;
  g_mutex_unlock(&trace->lock);
}

static gboolean cleanRtspSession(GstRTSPServer *server) {
  GstRTSPSessionPool *pool;

  __LOG(LOG_INFO, "[GST][%s:%d] rtsp session pool", _FILE_, __LINE__);

  pool = gst_rtsp_server_get_session_pool(server);
  gst_rtsp_session_pool_cleanup(pool);
  g_object_unref(pool);

  return TRUE;
}

static void client_closed(GstRTSPClient *client, gpointer user_data) {
  // CustomData *info = (CustomData*)user_data;
  // gchar *client_ip = (gchar *)user_data;
  const gchar *client_ip =
      gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

  __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__,
        client_ip);
  //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP2 : %s", _FILE_, __LINE__,
  //client_ip_2);

  // if(client_ip)g_free(client_ip);
}

static gboolean handle_client_connected(GstRTSPServer *server,
                                        GstRTSPClient *client,
                                        gpointer user_data) {
  // CustomData *info = (CustomData*)user_data;
  //  This function is called when a new client is connected to the RTSP server.
  //  You can handle the client connection here.
  const gchar *client_ip =
      gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));
  // GstRTSPUrl *url =
  // gst_rtsp_connection_get_url(gst_rtsp_client_get_connection(client));
  // GstRTSPMountPoints *mount = gst_rtsp_client_get_mount_points(client);
  // GstRTSPContext  *path = gst_rtsp_client_get_rtsp_context(client);
  // GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

  __LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__,
        client_ip);
  //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__,
  //url->host);
  //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__,
  //url->abspath);
  //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %d", _FILE_, __LINE__,
  //url->port);

  g_signal_connect(client, "closed", (GCallback)(client_closed), NULL);

  // g_free(client_ip);
  //  Initialize session for the new client

  return TRUE;
}

#if 0
/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  (void)media;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_INFO, "[RTSP][%s:%d] received source %p in session %p is active", _FILE_, __LINE__, source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    //g_print ("structure: %s\n", sstr);
    //__LOG(LOG_DEBUG, "[RTSP][%s:%d] received structure: %s", _FILE_, __LINE__, sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

static void on_sender_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  (void)media;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_INFO, "[RTSP][%s:%d] sender source %p in session %p is active", _FILE_, __LINE__, source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    //g_print ("Sender stats:\nstructure: %s\n", sstr);
    //__LOG(LOG_DEBUG, "[RTSP][%s:%d] Sender structure: %s", _FILE_, __LINE__, sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

#if 0
static void on_ssrc_active(GstElement *rtpbin, guint session_id, GstRTSPMedia *media) {
    g_print("SSRC is active, handling RTCP data\n");

    GstElement *rtcp_src = gst_bin_get_by_name(GST_BIN(rtpbin), "recv_rtcp_sink_0");

    if (rtcp_src) {
        GstBuffer *buffer;
        while (gst_pad_pull_range(gst_element_get_static_pad(rtcp_src, "sink"), 0, -1, &buffer) == GST_FLOW_OK) {
            // RTCP 데이터를 읽고 처리하거나 무시
            g_print("Discarded RTCP data\n");
            gst_buffer_unref(buffer);
        }
        gst_object_unref(rtcp_src);
    }
}
#endif

#include <gst/rtp/gstrtcpbuffer.h>
static gboolean on_receive_rtcp(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    GstRTCPBuffer rtcp_buffer = GST_RTCP_BUFFER_INIT;
    GstRTCPPacket packet;

    if (gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp_buffer)) {
        if (gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &packet)) {
            do {
                GstRTCPType type = gst_rtcp_packet_get_type(&packet);
                g_print("Received RTCP packet type: %d\n", type);
                __LOG(LOG_NOTICE, "[RTSP][%s:%d] Received RTCP packet type: %d", _FILE_, __LINE__, type);
            } while (gst_rtcp_packet_move_to_next(&packet));
        }
        gst_rtcp_buffer_unmap(&rtcp_buffer);
    }

    return GST_PAD_PROBE_OK;
}


static void on_identity_handoff(GstElement *identity, GstBuffer *buffer, GstPad *pad, gpointer user_data) {
    GstClockTime timestamp = GST_BUFFER_PTS(buffer);
    RtspServerData *info = (RtspServerData*)user_data;
    //printf("Formatted date with microseconds: %s\n", full_date);
    //if (GST_CLOCK_DIFF(last_timestamp_audio, timestamp) >= GST_SECOND) 
    {
        g_print("Timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(timestamp));
        info->last_timestamp = timestamp;
    }
}

static gboolean remove_func (GstRTSPSessionPool * pool, GstRTSPSession * session, GstRTSPServer * server)
{
  (void)pool;
  (void)session;
  (void)server;

  return GST_RTSP_FILTER_REMOVE;
}

static gboolean remove_sessions (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  __LOG(LOG_NOTICE, "[RTSP][%s:%d] removing all sessions", _FILE_, __LINE__);
  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_filter (pool, (GstRTSPSessionPoolFilterFunc) remove_func, server);
  g_object_unref (pool);

  return FALSE;
}
#endif

static void media_unprepared_cb(GstRTSPMedia *media, gpointer user_data) {
  RtspServerData *info = (RtspServerData *)user_data;
  GstElement *media_bin;
  GstElement *media_appsrc = NULL;
  GstElement *appsrc = NULL;

  __LOG(LOG_NOTICE, "[RTSP][%s:%d] ch%d media unprepared — clearing appsrc",
        _FILE_, __LINE__, info->ch);
  /* 이 media 소유의 appsrc만 정리 — 같은 채널의 새 media가 이미 게시한
   * 교체본을 옛 media의 unprepared가 지우는 것을 방지(세대 매칭) */
  media_bin = gst_rtsp_media_get_element(media);
  if (media_bin) {
    media_appsrc =
        gst_bin_get_by_name_recurse_up(GST_BIN(media_bin), info->appSrcName);
    gst_object_unref(media_bin);
  }
  /* 전역 추적 배열에서 제거 — ref 해제보다 먼저 수행해, lock 하에 배열
   * 항목을 본 쪽(SendEos)이 아직 살아있는 객체를 ref할 수 있게 한다 */
  g_mutex_lock(&g_rtsp_appsrc_lock);
  if (info->ch < MAX_RTSP_APPSRC && g_rtsp_appsrc[info->ch] == media_appsrc)
    g_rtsp_appsrc[info->ch] = NULL;
  g_mutex_unlock(&g_rtsp_appsrc_lock);
  /* 스트리밍 스레드가 스냅샷 ref를 쥐고 있을 수 있으므로 lock 안에서는
   * 포인터만 분리하고 unref는 lock 밖에서 수행한다 */
  g_mutex_lock(&info->lock);
  if (info->appsrc == media_appsrc) {
    appsrc = info->appsrc;
    info->appsrc = NULL;
  }
  g_mutex_unlock(&info->lock);
  if (appsrc)
    gst_object_unref(appsrc);
  if (media_appsrc)
    gst_object_unref(media_appsrc);
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb(GstRTSPMedia *media) {
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams(media);

  // GST_INFO ("media %p is prepared and has %u streams", media, n_streams);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] media %p is prepared and has %u streams",
        _FILE_, __LINE__, media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream(media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession(stream);
    // GST_INFO ("watching session %p on stream %u", session, i);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] watching session %p on stream %u", _FILE_,
          __LINE__, session, i);

    // g_signal_connect (session, "on-ssrc-active", (GCallback) on_ssrc_active,
    // media); g_signal_connect (session, "on-sender-ssrc-active", (GCallback)
    // on_sender_ssrc_active, media); GstElement *rtpbin =
    // gst_rtsp_media_get_element(media); g_signal_connect(rtpbin,
    // "on-ssrc-active", (GCallback)on_ssrc_active, media);
    // gst_object_unref(rtpbin);
  }
}

static void media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media,
                            gpointer user_data) {
  // GstElement *src = (GstElement*)user_data;
  RtspServerData *info = (RtspServerData *)user_data;
  GstElement *element;
  GstElement *appsrc = NULL;
  GstElement *out_queue = NULL;
  GstElement *old_appsrc = NULL;
  GstCaps *fallback_caps = NULL;
  // GstElement *queue;
  // gchar *queue_name;
  // static GMutex mutex;

  // g_mutex_lock(&mutex);
  __LOG(LOG_NOTICE, "[GST][%s:%d] media connect ch:%d", _FILE_, __LINE__,
        info->ch);
  element = gst_rtsp_media_get_element(media);
  // name = GST_ELEMENT_NAME(GST_APP_SRC(element));
  // name = gst_object_get_name(GST_OBJECT(element));
  __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__,
        info->appSrcName);
  appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), info->appSrcName);
  if (appsrc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] appsrc is null", _FILE_, __LINE__);
    /* 조회 실패 시에도 이전 appsrc를 비워 stale push를 막는다.
     * 전역 배열의 같은 항목도 ref 해제 전에 정리(댕글링 방지) */
    g_mutex_lock(&info->lock);
    old_appsrc = info->appsrc;
    info->appsrc = NULL;
    g_mutex_unlock(&info->lock);
    g_mutex_lock(&g_rtsp_appsrc_lock);
    if (info->ch < MAX_RTSP_APPSRC && g_rtsp_appsrc[info->ch] == old_appsrc)
      g_rtsp_appsrc[info->ch] = NULL;
    g_mutex_unlock(&g_rtsp_appsrc_lock);
    if (old_appsrc)
      gst_object_unref(old_appsrc);
    goto media_configure_out;
  }
  {
    const gboolean use_h265 = g_strcmp0(cmdArg.enc, ENC_H265) == 0;
    fallback_caps = gst_caps_from_string(
        use_h265 ? "video/x-h265,stream-format=byte-stream,alignment=au"
                 : "video/x-h264,stream-format=byte-stream,alignment=au");
  }
  /* appsrc를 streaming thread에 게시하기 전에 probe를 먼저 설치해 첫 push부터
   * 빠짐없이 측정한다. trace와 stall이 모두 꺼져 있으면 즉시 반환한다. */
  rtsp_sync_install_media_probes(info, element, appsrc);
  if (info->sync_trace) {
    out_queue =
        gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_out_queue");
    if (out_queue) {
      g_signal_connect(out_queue, "overrun",
                       (GCallback)rtsp_factory_queue_overrun, info);
      __LOG(LOG_NOTICE,
            "[RTSP_SYNC][%s:%d] ch=%u rtsp_out_queue overrun callback connected",
            _FILE_, __LINE__, info->ch);
      gst_object_unref(out_queue);
    } else {
      __LOG(LOG_ERR, "[RTSP_SYNC][%s:%d] ch=%u rtsp_out_queue not found",
            _FILE_, __LINE__, info->ch);
    }
  }
  /* appsrc 게시와 caps 적용을 한 임계구역에서 수행 — 스트리밍 스레드의
   * caps 갱신(g_object_set)과 전순서를 보장해 stale caps 덮어쓰기를 막는다 */
  g_mutex_lock(&info->lock);
  old_appsrc = info->appsrc;
  info->appsrc = appsrc;
  rtsp_sync_activate(info);
  /* 새 appsrc는 빈 큐로 시작하므로 플로우 컨트롤 상태 리셋 */
  g_atomic_int_set(&info->appsrc_full, 0);
  info->wait_keyframe = FALSE;
  if (info->caps)
    g_object_set(appsrc, "caps", info->caps, NULL);
  else if (fallback_caps)
    g_object_set(appsrc, "caps", fallback_caps, NULL);
  g_mutex_unlock(&info->lock);
  /* 전역 추적 배열 갱신을 old ref 해제보다 먼저 수행 — 배열이 해제된
   * 객체를 가리키는 창을 없앤다 (SendEos의 lock 하 ref 안전 보장) */
  g_mutex_lock(&g_rtsp_appsrc_lock);
  if (info->ch < MAX_RTSP_APPSRC) {
    g_rtsp_appsrc[info->ch] = appsrc;
    if (info->ch >= g_rtsp_appsrc_count)
      g_rtsp_appsrc_count = info->ch + 1;
  }
  g_mutex_unlock(&g_rtsp_appsrc_lock);
  if (old_appsrc)
    gst_object_unref(old_appsrc);
  if (fallback_caps)
    gst_caps_unref(fallback_caps);
  /* 새 media 접속 — GOP 위상과 무관하게 즉시 IDR을 받아 prepare(SDP용
   * SPS/PPS/IDR 대기)와 첫 화면 표시까지의 지연을 단축한다 */
  request_keyframe(info);
  // queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__,
  //appsrc_name); queue = gst_bin_get_by_name_recurse_up(GST_BIN(element),
  // queue_name);

  // g_object_set (G_OBJECT (queue),
  // g_free(queue_name);

  if (cmdArg.dbg_rtsp_ts) {
    GstElement *pay = gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay0");
    if (pay) {
      if (g_object_get_data(G_OBJECT(pay), "rtsp-pay-probe-added")) {
        gst_object_unref(pay);
        goto media_configure_out;
      }

      GstPad *pay_src_pad = gst_element_get_static_pad(pay, "src");
      if (pay_src_pad) {
        RtspPayProbeCtx *ctx =
            (RtspPayProbeCtx *)g_malloc0(sizeof(RtspPayProbeCtx));
        ctx->info = info;
        ctx->media_element = (GstElement *)gst_object_ref(element);
        ctx->last_log_us = 0;
        gst_pad_add_probe(pay_src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                          (GstPadProbeCallback)rtsp_pay_src_probe, ctx,
                          rtsp_pay_probe_ctx_free);
        g_object_set_data(G_OBJECT(pay), "rtsp-pay-probe-added",
                          GINT_TO_POINTER(1));
        gst_object_unref(pay_src_pad);
      }
      gst_object_unref(pay);
    } else {
      __LOG(LOG_INFO, "[RTSP][%s:%d] ch%d pay0 not found (no rtsp-out-latency)",
            _FILE_, __LINE__, info->ch);
    }
  }

media_configure_out:
  gst_object_unref(element);

  g_signal_connect(media, "prepared", (GCallback)media_prepared_cb, factory);
  g_signal_connect(media, "unprepared", (GCallback)media_unprepared_cb, info);
  if (appsrc) {
    g_signal_connect(appsrc, "enough-data", (GCallback)enough_data, info);
    g_signal_connect(appsrc, "need-data", (GCallback)need_data, info);
  }

  // g_object_set(element, "rtcp-min-interval", 10.0, NULL);
  // g_object_set(element, "rtcp-max-interval", 60.0, NULL);
#if 0
    GstPad *pad = gst_element_get_static_pad(element, "recv_rtcp_sink_0");
    if (pad) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] pad add : recv_rtcp_sink_0", _FILE_, __LINE__);
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) on_receive_rtcp, user_data, NULL);
        gst_object_unref(pad);
    }
#endif
  // gst_object_unref(rtpbin);
  // g_mutex_unlock(&mutex);

#ifdef DYNAMIC_CAPS
  GstCaps *caps;

  g_object_get(info->appsrc, "caps", &caps, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__,
        info->ch, gst_caps_to_string(caps));
  g_object_set(info->appsrc, "caps", info->caps, NULL);
  g_object_get(info->appsrc, "caps", &caps, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set caps : %s", _FILE_, __LINE__,
        info->ch, gst_caps_to_string(caps));

  gst_caps_unref(caps);
#endif

  return;
}

static void eos_callback(GstAppSink *appsink, gpointer user_data) {
  RtspServerData *info = (RtspServerData *)user_data;

  __LOG(LOG_INFO, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, info->ch,
        __FUNCTION__);
  is_interrupted = TRUE;
}

static GstFlowReturn new_sample_handler(GstElement *sink, gpointer userData) {
  GstSample *sample;
  GstBuffer *buffer;
  RtspServerData *info = (RtspServerData *)userData;
  GstCaps *sample_caps;
  gboolean caps_changed = FALSE;

  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
#ifdef DYNAMIC_CAPS
  if (!info->caps) {
    info->caps =
        gst_pad_get_current_caps(gst_element_get_static_pad(sink, "sink"));
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch %d caps : %s", _FILE_, __LINE__,
    //__FUNCTION__, info->ch, gst_caps_to_string(info->caps)); g_print("ch%d
    // caps : %s\n", info->ch, gst_caps_to_string(info->caps));
  }
#endif

  sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  if (!sample) {
    //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_,
    //__LINE__);
    return GST_FLOW_ERROR;
  }
  buffer = gst_sample_get_buffer(sample);
  if (!buffer) {
    __LOG(LOG_CRIT, "[RTSP][%s:%d] ch%d buffer cannot get from sample",
          _FILE_, __LINE__, info->ch);
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }
  // gst_sample_unref(sample);

  g_mutex_lock(&info->lock);
  sample_caps = gst_sample_get_caps(sample);
  if (sample_caps) {
    if (!info->caps || !gst_caps_is_equal(info->caps, sample_caps)) {
      if (info->caps)
        gst_caps_unref(info->caps);
      info->caps = gst_caps_copy(sample_caps);
      caps_changed = TRUE;
    }
  }
  /* appsrc caps 쓰기는 media_configure()와 같은 lock 안에서 수행해
   * 전순서를 보장한다 (stale caps 덮어쓰기 방지) */
  if (caps_changed && info->caps && info->appsrc)
    g_object_set(info->appsrc, "caps", info->caps, NULL);
  /* appsrc 큐가 가득 차면(enough-data) 비워질 때까지(need-data) 드롭하고
   * 재개는 키프레임부터 — PLAY 이전 구간에서 appsrc 내부 큐에 백로그가
   * 무제한으로 쌓여 재생 시작이 수 초 지연되는 것을 방지한다 */
  gboolean drop = FALSE;
  gboolean want_key = FALSE;
  gboolean drop_full = FALSE;
  gboolean drop_key_wait = FALSE;
  gboolean has_appsrc = info->appsrc != NULL;
  if (g_atomic_int_get(&info->appsrc_full)) {
    info->wait_keyframe = TRUE;
    drop = TRUE;
    drop_full = TRUE;
  } else if (info->wait_keyframe) {
    if (buffer && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
      info->wait_keyframe = FALSE;
    else {
      drop = TRUE;
      want_key = TRUE;
      drop_key_wait = TRUE;
    }
  }
  /* lock 밖 push 동안 쓸 스냅샷 — appsrc는 media_unprepared_cb가 언제든
   * 마지막 ref를 해제할 수 있으므로 ref로 수명을 고정한다 */
  GstElement *appsrc = (!drop && info->appsrc)
                           ? (GstElement *)gst_object_ref(info->appsrc)
                           : NULL;
  g_mutex_unlock(&info->lock);

  guint64 origin_index = 0;
  RtspSyncTrace *sync_trace = (RtspSyncTrace *)info->sync_trace;
  gboolean trace_buffer = rtsp_sync_bridge_input(
      sync_trace, buffer, has_appsrc, drop_full, drop_key_wait,
      is_interrupted, &origin_index);

  /* 스로틀로 유실된 IDR 요청의 지연 재시도 — 키프레임 대기 중이면 매 프레임
   * 재요청하되, request_keyframe 내부 스로틀이 실제 전송을 1회/초로 제한 */
  if (want_key)
    request_keyframe(info);

#if 0
    GstFlowReturn ret;
    g_signal_emit_by_name(info->appsrc, "push-buffer", buffer, &ret);
    if(ret != GST_FLOW_OK)
    {
        g_print("Failed to push buffer\n");
    }
#endif

#if 1
  if (appsrc == NULL || is_interrupted) {
    // if(info->ch == 0) g_print("ch%d appsrc null return!\n", info->ch);
    if (appsrc)
      gst_object_unref(appsrc);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
#endif

  if (info->debug) {
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime delta = GST_CLOCK_TIME_NONE;
    GstClockTime pipe_latency = GST_CLOCK_TIME_NONE;

    if (GST_CLOCK_TIME_IS_VALID(pts) &&
        GST_CLOCK_TIME_IS_VALID(info->last_timestamp) &&
        pts >= info->last_timestamp) {
      delta = pts - info->last_timestamp;
    }
    info->last_timestamp = pts;

    if (pipeline && GST_CLOCK_TIME_IS_VALID(pts)) {
      GstClock *clock = gst_element_get_clock(pipeline);
      if (clock) {
        GstClockTime now = gst_clock_get_time(clock);
        gst_object_unref(clock);
        GstClockTime base = gst_element_get_base_time(pipeline);
        GstClockTime running = (now > base) ? (now - base) : 0;
        pipe_latency = (running >= pts) ? (running - pts) : 0;
      }
    }

    // Throttle logs to avoid flooding (at most once per second per channel)
    gint64 now_us = g_get_monotonic_time();
    if (info->last_log_us == 0 || (now_us - info->last_log_us) >= 1000000) {
      info->last_log_us = now_us;
      __LOG(LOG_NOTICE,
            "[RTSP][%s:%d] ch%d pipe-latency=%" GST_TIME_FORMAT
            " pts=%" GST_TIME_FORMAT " delta=%" GST_TIME_FORMAT,
            _FILE_, __LINE__, info->ch, GST_TIME_ARGS(pipe_latency),
            GST_TIME_ARGS(pts), GST_TIME_ARGS(delta));
    }
  }

  // Zero-Copy Optimization (Ref instead of Copy)
  // Use gst_buffer_copy_region with GST_BUFFER_COPY_MEMORY to share memory but
  // strip timestamps/meta. This ensures appsrc can assign fresh timestamps
  // (do-timestamp=1) avoiding sync issues.
  GstBuffer *out_buffer =
      gst_buffer_copy_region(buffer, GST_BUFFER_COPY_MEMORY, 0, -1);
  gboolean stripped_pts_valid =
      out_buffer && GST_BUFFER_PTS_IS_VALID(out_buffer);
  gboolean meta_ok = TRUE;
  if (out_buffer && trace_buffer && GST_BUFFER_PTS_IS_VALID(buffer)) {
    meta_ok = gst_buffer_add_reference_timestamp_meta(
                  out_buffer, sync_trace->reference, GST_BUFFER_PTS(buffer),
                  origin_index) != NULL;
  }
  rtsp_sync_record_copy(sync_trace, trace_buffer, out_buffer != NULL,
                        stripped_pts_valid, meta_ok);

  if (!out_buffer) {
    __LOG(LOG_ERR, "[RTSP_SYNC][%s:%d] ch%d buffer copy failed",
          _FILE_, __LINE__, info->ch);
    gst_object_unref(appsrc);
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  if (rtsp_frame_id_sei_enabled()) {
    GstClockTime sei_start_ns = GST_CLOCK_TIME_NONE;
    if (sync_trace && trace_buffer)
      sei_start_ns = gst_util_get_timestamp();
    GstBuffer *sei_buffer = rtsp_frame_id_sei_insert(
        info, out_buffer, GST_BUFFER_PTS(buffer));
    GstClockTime sei_elapsed_ns = GST_CLOCK_TIME_NONE;
    if (GST_CLOCK_TIME_IS_VALID(sei_start_ns)) {
      GstClockTime sei_end_ns = gst_util_get_timestamp();
      if (sei_end_ns >= sei_start_ns)
        sei_elapsed_ns = sei_end_ns - sei_start_ns;
    }
    rtsp_sync_record_frame_id_sei(
        sync_trace, trace_buffer, sei_buffer != NULL, sei_elapsed_ns);
    if (sei_buffer) {
      gst_buffer_unref(out_buffer);
      out_buffer = sei_buffer;
      info->frame_id_sei_inserted++;
      if (info->frame_id_sei_inserted == 1) {
        __LOG(LOG_NOTICE,
              "[RTSP_FRAME_ID][%s:%d] ch=%u H.265 SEI enabled"
              " origin_pts_ns=%" G_GUINT64_FORMAT,
              _FILE_, __LINE__, info->ch, GST_BUFFER_PTS(buffer));
      }
    } else {
      info->frame_id_sei_failed++;
      if (info->frame_id_sei_failed == 1) {
        __LOG(LOG_ERR,
              "[RTSP_FRAME_ID][%s:%d] ch=%u H.265 SEI insertion failed",
              _FILE_, __LINE__, info->ch);
      }
    }
  }

  /* NO_SHARE 메모리이거나 EXCLUSIVE lock 획득 실패 시 위 copy_region이 실제
   * memcpy로 동작하므로, 채널당 1회 메모리 공유 여부를 남겨 zero-copy를
   * 확인한다 (shared=1이면 원본 GstMemory를 ref 공유 = 복사 없음) */
  if (!info->mem_flags_logged && out_buffer &&
      gst_buffer_n_memory(buffer) > 0 && gst_buffer_n_memory(out_buffer) > 0) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    info->mem_flags_logged = TRUE;
    __LOG(LOG_NOTICE,
          "[RTSP][%s:%d] ch%d first pushed buffer mem: flags=0x%x no-share=%d "
          "shared=%d allocator=%s n_mem=%u size=%" G_GSIZE_FORMAT,
          _FILE_, __LINE__, info->ch, GST_MEMORY_FLAGS(mem),
          (GST_MEMORY_FLAGS(mem) & GST_MEMORY_FLAG_NO_SHARE) ? 1 : 0,
          gst_buffer_peek_memory(out_buffer, 0) == mem ? 1 : 0,
          mem->allocator ? mem->allocator->mem_type : "(none)",
          gst_buffer_n_memory(buffer), gst_buffer_get_size(buffer));
  }

  // Push the buffer (takes ownership)
  GstFlowReturn push_ret =
      gst_app_src_push_buffer(GST_APP_SRC(appsrc), out_buffer);
  rtsp_sync_record_push(sync_trace, trace_buffer, push_ret);
  if (push_ret != GST_FLOW_OK) {
    __LOG(LOG_WARNING, "[RTSP][%s:%d] ch%d appsrc push failed: %d", _FILE_,
          __LINE__, info->ch, push_ret);
  }

  gst_object_unref(appsrc);
  gst_sample_unref(sample);

  return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) {
  RtspServerData *info = (RtspServerData *)data;
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        info->ch);
  info->start_f = TRUE;

  return GST_FLOW_OK;
}

gboolean RtspServerBin::getStartFlag() { return rtspServerData.start_f; }

void RtspServerBin::setOverlayText(gchar *text) {
  if (re.overlay == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d overlay not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }
  g_object_set(re.overlay, "text", text, NULL);
}

void RtspServerBin::setTimeStampDebug() {
  rtspServerData.debug = !rtspServerData.debug;
}

void RtspServerBin::getBitrate() {
  gint bps;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  g_object_get(re.enc, "bitrate", &bps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__,
  //ch, bps);
  g_print("rtsp ch%d get bitrate : %d\n", rtspServerData.ch, bps);
}

void RtspServerBin::setBitrate(gint data) {
  gint bps;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  g_object_set(re.enc, "bitrate", data, NULL);
  g_object_get(re.enc, "bitrate", &bps, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set bitrate : %d", _FILE_, __LINE__,
        rtspServerData.ch, bps);
  g_print("rtsp ch%d set bitrate : %d\n", rtspServerData.ch, bps);
}

void RtspServerBin::getFps() {
  // gint fps;
  GstCaps *caps;

  if (re.capsfilter == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d capsfilter not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  g_object_get(re.capsfilter, "caps", &caps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get fps : %s", _FILE_, __LINE__, ch,
  //gst_caps_to_string(caps));
  g_print("rtsp ch%d get fps : %s\n", rtspServerData.ch,
          gst_caps_to_string(caps));

  gst_caps_unref(caps);
}

void RtspServerBin::setFps(guint16 data) {
  // gint fps;
  GstCaps *caps;
  gchar *caps_str;

  /* Check if properly initialized (rtspServerData.ch is set during init) */
  if (rtspServerData.ch >= MAX_CHANNEL) {
    __LOG(LOG_ERR, "[GST][%s:%d] rtspServerBin not initialized (ch=%d)", _FILE_, __LINE__, rtspServerData.ch);
    g_print("rtsp ch%d not initialized\n", rtspServerData.ch);
    return;
  }

#if 0
    g_object_get(re.rate, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get max-rate : %d", _FILE_, __LINE__, rtspServerData.ch, fps);
    g_object_set(re.rate, "max-rate", data, NULL);
    g_object_get(re.rate, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set max-rate : %d", _FILE_, __LINE__, rtspServerData.ch, fps);
#endif

  /* Check if properly initialized - verify videorate element exists */
  if (re.rate == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d videorate not initialized, cannot set fps", 
            _FILE_, __LINE__, rtspServerData.ch);
    return;
  }

  /* Only change videorate max-rate, don't touch capsfilter to avoid renegotiation */
  gint current_fps;
  g_object_get(re.rate, "max-rate", &current_fps, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d current max-rate=%d, setting to %d", 
          _FILE_, __LINE__, rtspServerData.ch, current_fps, data);
  g_object_set(re.rate, "max-rate", data, NULL);
  
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set max-rate=%d (videorate only, no caps renegotiation)", 
          _FILE_, __LINE__, rtspServerData.ch, data);
}

void RtspServerBin::getCaps() {
  GstCaps *caps;
  GstElement *appsrc;

  g_mutex_lock(&rtspServerData.lock);
  appsrc = rtspServerData.appsrc
               ? (GstElement *)gst_object_ref(rtspServerData.appsrc)
               : NULL;
  g_mutex_unlock(&rtspServerData.lock);
  if (appsrc == NULL)
    return;

  g_object_get(appsrc, "caps", &caps, NULL);
  // g_object_get(info->appsrc, "caps", &caps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch,
  //gst_caps_to_string(caps));
  g_print("rtsp ch%d get caps : %s\n", rtspServerData.ch,
          gst_caps_to_string(caps));
  gst_caps_unref(caps);
  gst_object_unref(appsrc);
}

void RtspServerBin::setRotation(guint16 data) {
  gint rotation;

  if (re.convert == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d converter not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  // g_object_get(re.enc, "bitrate", &bps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__,
  //ch, bps);
  g_object_set(re.convert, "rotation", data, NULL);
  g_object_get(re.convert, "rotation", &rotation, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set rotation : %d", _FILE_, __LINE__,
        rtspServerData.ch, rotation);
  g_print("rtsp ch%d set rotation : %d\n", rtspServerData.ch, rotation);
}

void RtspServerBin::getRotation() {
  gint rotation;

  if (re.convert == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d converter not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  g_object_get(re.convert, "rotation", &rotation, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get rotation : %d", _FILE_, __LINE__,
  //ch, rotation);
  g_print("rtsp ch%d get rotation : %d\n", rtspServerData.ch, rotation);
}

void RtspServerBin::forceKeyframe() {
  if (re.enc == NULL)
    return;

  GstEvent *event =
      gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0);
  if (!gst_element_send_event(re.enc, event))
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d force keyframe send failed", _FILE_,
          __LINE__, rtspServerData.ch);
}

void RtspServerBin::setGop(guint16 data) {
  gint gop;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  // g_object_get(re.enc, "bitrate", &bps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__,
  //ch, bps);
  g_object_set(re.enc, "gop-size", data, NULL);
  g_object_get(re.enc, "gop-size", &gop, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set gop_size : %d", _FILE_, __LINE__,
        rtspServerData.ch, gop);
  g_print("rtsp ch%d set gop_size : %d\n", rtspServerData.ch, gop);
}

void RtspServerBin::getGop() {
  gint gop;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  g_object_get(re.enc, "gop-size", &gop, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get gop_size : %d", _FILE_, __LINE__,
  //ch, gop);
  g_print("rtsp ch%d get gop_size : %d\n", rtspServerData.ch, gop);
}

void RtspServerBin::getKeyframe() {
  gint key;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  // g_object_get(re.enc, "bitrate", &bps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__,
  //ch, bps);
  g_object_get(re.enc, "set-keyframe", &key, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get keyframe : %d", _FILE_, __LINE__,
  //ch, key);
  g_print("rtsp ch%d get keyframe : %d\n", rtspServerData.ch, key);
}

void RtspServerBin::setkeyframe(guint16 data) {
  gint key;

  if (re.enc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_,
          __LINE__, rtspServerData.ch);
    return;
  }

  // g_object_get(re.enc, "bitrate", &bps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__,
  //ch, bps);
  g_object_set(re.enc, "set-keyframe", data, NULL);
  g_object_get(re.enc, "set-keyframe", &key, NULL);
  __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set keyframe : %d", _FILE_, __LINE__,
        rtspServerData.ch, key);
  g_print("rtsp ch%d set keyframe : %d\n", rtspServerData.ch, key);
}

GstState RtspServerBin::getState() {
  GstState state;
  gst_element_get_state(re.bin, &state, NULL, GST_CLOCK_TIME_NONE);
  return state;
}

GstStateChangeReturn RtspServerBin::setState(GstState state) {
  return gst_element_set_state(re.bin, state);
}

RtspServerBin *RtspServerBin::getInstance() {
  static RtspServerBin instance;
  return &instance;
}

RtspServerBin::RtspServerBin() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  sinkPad = NULL;
  re.bin = NULL;
  rtspServerData.caps = NULL;
  rtspServerData.appsrc = NULL;
  rtspServerData.start_f = FALSE;
  rtspServerData.debug = FALSE;
  rtspServerData.last_timestamp = GST_CLOCK_TIME_NONE;
  rtspServerData.last_log_us = 0;
  rtspServerData.dual_bps = TRUE;
  rtspServerData.mem_flags_logged = FALSE;
  rtspServerData.appsrc_full = 0;
  rtspServerData.wait_keyframe = FALSE;
  rtspServerData.last_enough_log_us = 0;
  rtspServerData.kick_sink = NULL;
  rtspServerData.last_kick_us = 0;
  rtspServerData.sync_trace = NULL;
  rtspServerData.frame_id_parser = NULL;
  rtspServerData.frame_id_sei_inserted = 0;
  rtspServerData.frame_id_sei_failed = 0;
  g_mutex_init(&rtspServerData.lock);
}

RtspServerBin::~RtspServerBin() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        rtspServerData.ch);
  rtsp_sync_trace_free((RtspSyncTrace *)rtspServerData.sync_trace);
  if (rtspServerData.frame_id_parser)
    gst_h265_parser_free(
        (GstH265Parser *)rtspServerData.frame_id_parser);
  g_mutex_clear(&rtspServerData.lock);
}

gboolean RtspServerBin::addBinToPipe(GstElement *pipe) {
  if (gst_bin_get_by_name(GST_BIN(pipe),
                          g_strdup_printf("captureBin%d", rtspServerData.ch)) !=
      NULL) {
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d capture bin is already added", _FILE_,
          __LINE__, rtspServerData.ch);
    return 1;
  }

  return gst_bin_add(GST_BIN(pipe), re.bin);
}

gboolean RtspServerBin::removeBinToPipe(GstElement *pipe) {
  return gst_bin_remove(GST_BIN(pipe), re.bin);
}

GstPad *RtspServerBin::getBinSinkPad() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        rtspServerData.ch);
  // return gst_element_get_static_pad(re.bin,
  // g_strdup_printf("recordBin_sink_ch%d", ch));
  return sinkPad;
}

gboolean RtspServerBin::addBinTeeRecordPad(guint8 ch) {
  __LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
  
  GstPad *target_pad = gst_element_get_request_pad(re.tee, "src_%u");
  teeRecordPad =
      gst_ghost_pad_new(g_strdup_printf("record_pad_ch%d", ch),
                        target_pad);
  if (target_pad)
      gst_object_unref(target_pad);

  return gst_element_add_pad(re.bin, teeRecordPad);
}

GstPad *RtspServerBin::getBinTeeRecordPad() {
  if (teeRecordPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, rtspServerData.ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          rtspServerData.ch);

  return teeRecordPad;
}

gboolean RtspServerBin::getDualBps() { return rtspServerData.dual_bps; }

void RtspServerBin::setDualBps(gboolean val) {
  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d val:%d", _FILE_, __LINE__,
  //__FUNCTION__, rtspServerData.ch, val);
  rtspServerData.dual_bps = val;
}

gboolean RtspServerBin::audioInit() {
  gboolean ret = 0;
  GstPad *staticPad;

  if (re.bin != NULL)
    return ret;

  __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

  rtspServerData.ch = 4;
  // den = rnnoise_create(NULL);
  re.bin = gst_bin_new(g_strdup_printf("rtspServerBin%d", rtspServerData.ch));
  re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
  re.sink = gst_element_factory_make("appsink", "appsink");
  re.identity = gst_element_factory_make("identity", "identity");

  if (!re.bin || !re.queue || !re.sink) {
    __LOG(LOG_CRIT, "[GST][%s:%d] audio rtsp element create error", _FILE_,
          __LINE__);
    return ret;
  }

  gst_bin_add_many(GST_BIN(re.bin), re.queue, re.sink, NULL);

  ret = gst_bin_add(GST_BIN(pipeline), re.bin);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] audio rtsp bin add error in pipeline", _FILE_,
          __LINE__);
    return ret;
  }

  ret = gst_element_link_many(re.queue, re.sink, NULL);

  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp link err", _FILE_, __LINE__);
    return ret;
  }

  const gint appsink_max_buffers =
      clamp_int(cmdArg.rtsp_appsink_max_buffers, 1, 120);
  g_object_set(re.sink, "max-buffers", appsink_max_buffers, NULL);
  g_object_set(re.sink, "drop", TRUE, NULL);
  // g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
  // g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
  g_object_set(re.sink, "emit-signals", TRUE, "sync", FALSE, "async", FALSE,
               NULL);
  // g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);

  g_signal_connect(re.sink, "eos", G_CALLBACK(eos_callback), &rtspServerData);
  g_signal_connect(re.sink, "new-sample", G_CALLBACK(new_sample_handler),
                   &rtspServerData);
  g_signal_connect(re.sink, "new-preroll", G_CALLBACK(new_preroll_handler),
                   &rtspServerData);
  // g_signal_connect(re.identity, "handoff", G_CALLBACK(on_identity_handoff),
  // &rtspServerData);

  staticPad = gst_element_get_static_pad(re.queue, "sink");
  sinkPad = gst_ghost_pad_new(
      g_strdup_printf("rtspServerBin_sink_ch%d", rtspServerData.ch), staticPad);

  ret = gst_element_add_pad(re.bin, sinkPad);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp pad add err", _FILE_, __LINE__);
    return ret;
  }

  gst_object_unref(staticPad);

  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  // rtspServerData.appSrcName = g_strdup_printf("%s%d", "appsrc", ch);
  rtspServerData.appSrcName = g_strdup_printf("%s", "audio_rtsp_appsrc");

  // gchar *launch_str = g_strdup_printf("( appsrc name=%s ! queue
  // max-size-buffers=60 leaky=2 ! vpuenc_h264 bitrate=1024 ! h264parse
  // config-interval=-1 ! rtph264pay name=pay0 config-interval=-1 )",
  // rtspServerData.appSrcName); gchar *launch_str = g_strdup_printf("appsrc
  // name=%s ! rtpjitterbuffer latency=200 ! rtpmp4adepay ! mpegaudioparse !
  // rtpmpapay name=pay0 pt=97 )", rtspServerData.appSrcName);
  const gint q_max_buffers =
      clamp_int(cmdArg.rtsp_factory_queue_max_buffers, 1, 120);
  gchar *launch_str = g_strdup_printf(
      "appsrc name=%s do-timestamp=1 is-live=1 format=3 max-bytes=16384 "
      "! queue max-size-buffers=%d max-size-time=0 max-size-bytes=0 leaky=2 ! "
      "mpegaudioparse ! rtpmpapay name=pay0 pt=97 config-interval=-1 )",
      rtspServerData.appSrcName, q_max_buffers);
  // gchar *launch_str = g_strdup_printf("appsrc name=%s do-timestamp=1
  // is-live=1 format=3 ! queue max-size-buffers=15 leaky=2 ! mpegaudioparse !
  // rtpmpapay name=pay0 pt=97 )", rtspServerData.appSrcName);

  gst_rtsp_media_factory_set_launch(factory, launch_str);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  const gint factory_latency_ms =
      clamp_int(cmdArg.rtsp_factory_latency_ms, 1, 2000);
  gst_rtsp_media_factory_set_latency(factory, factory_latency_ms);
  g_free(launch_str);

  log_once(LOG_INFO,
           g_strdup_printf("[GST][%s:%d] eos shutdown : %s", _FILE_, __LINE__,
                           gst_rtsp_media_factory_is_eos_shutdown(factory)
                               ? "TRUE"
                               : "FALSE"));
  log_once(LOG_INFO,
           g_strdup_printf("[GST][%s:%d] latency : %d", _FILE_, __LINE__,
                           gst_rtsp_media_factory_get_latency(factory)));
  // rtspServerData.appsrc = gst_element_factory_make("appsrc",
  // rtspServerData.appSrcName);
  g_signal_connect(factory, "media-configure", (GCallback)media_configure,
                   &rtspServerData);

  gchar *point = g_strdup_printf("/ch%d", rtspServerData.ch);
  //__LOG(LOG_INFO, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

  gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

  gst_rtsp_media_factory_add_role(
      factory, cmdArg.rtsp_id, GST_RTSP_PERM_MEDIA_FACTORY_ACCESS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT,
      G_TYPE_BOOLEAN, TRUE, NULL);

  __LOG(LOG_NOTICE, "[RTSP][%s:%d]stream ready at rtsp://%s:%s@127.0.0.1:%s%s",
        _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_passwd, cmdArg.rtsp_port,
        point);

  g_free(point);

  return ret;
}

#if 0
gboolean RtspServerBin::init(guint8 ch, gboolean crop_en)
{
    gboolean ret = 0;
    const gboolean use_h265 = g_strcmp0(cmdArg.enc, ENC_H265) == 0;
    const gchar *parser_factory = use_h265 ? "h265parse" : "h264parse";
    const gchar *payloader_factory = use_h265 ? "rtph265pay" : "rtph264pay";
    rtspServerData.ch = ch;
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d, crop : %s", _FILE_, __LINE__, __FUNCTION__, rtspServerData.ch, crop_en? "enable":"disable");

    re.bin = gst_bin_new(g_strdup_printf("rtspServerBin%d", rtspServerData.ch));
    re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    re.sink = gst_element_factory_make("appsink", "appsink");

    if (!re.bin || !re.sink || !re.queue) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.sink, NULL);
    
    //gst_bin_add_many(GST_BIN(re.bin), re.convert2, re.compositor, re.capsfilter2, re.videoflip, re.convert2, NULL);

    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if(!ret) { 
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }
    ret = gst_element_link_many(re.queue, re.sink, NULL);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp link err", _FILE_, __LINE__);
        return ret;
    }

    //g_object_set(re.sink, "max-buffers", cmdArg.fps[STREAM_RTSP][ch], NULL);
    //g_object_set(re.sink, "drop", TRUE, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(re.sink, "emit-signals", TRUE, "sync", TRUE, NULL);
    //g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);

    g_signal_connect(re.sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(re.sink, "new-sample", G_CALLBACK(new_sample_handler), &rtspServerData);
    g_signal_connect(re.sink, "new-preroll", G_CALLBACK(new_preroll_handler), &rtspServerData);
    //g_signal_connect(re.identity, "handoff", G_CALLBACK(on_identity_handoff), &rtspServerData);

    sinkPad = gst_ghost_pad_new(g_strdup_printf("rtspServerBin_sink_ch%d", rtspServerData.ch), gst_element_get_static_pad(re.queue, "sink"));

    ret = gst_element_add_pad(re.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp pad add err", _FILE_, __LINE__);
        return ret;
    }


    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    //rtspServerData.appSrcName = g_strdup_printf("%s%d", "appsrc", ch);
    rtspServerData.appSrcName = g_strdup_printf("%s%d", "rtsp_appsrc", ch);

    //gchar *launch_str = g_strdup_printf("( appsrc name=%s ! queue max-size-buffers=60 leaky=2 ! vpuenc_h264 bitrate=1024 ! h264parse config-interval=-1 ! rtph264pay name=pay0 config-interval=-1 )", rtspServerData.appSrcName);
    gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=%d leaky=2 ! %s ! %s name=pay0 config-interval=-1 )", rtspServerData.appSrcName, cmdArg.fps[STREAM_RTSP][ch], parser_factory, payloader_factory);
    //gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 block=true ! queue max-size-buffers=5 leaky=2 min-threshold-time=500000000 ! h264parse ! rtph264pay name=pay0 config-interval=0 )", rtspServerData.appSrcName);

    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    const gint factory_latency_ms = clamp_int(cmdArg.rtsp_factory_latency_ms, 1, 2000);
    gst_rtsp_media_factory_set_latency(factory, factory_latency_ms);
    g_free(launch_str);
    
    log_once(LOG_INFO, g_strdup_printf("[GST][%s:%d] eos shutdown : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_eos_shutdown(factory)? "TRUE":"FALSE"));
    log_once(LOG_INFO, g_strdup_printf("[GST][%s:%d] latency : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_latency(factory)));
    //rtspServerData.appsrc = gst_element_factory_make("appsrc", rtspServerData.appSrcName);
    g_signal_connect(factory, "media-configure", (GCallback)media_configure, &rtspServerData);

    gchar *point = g_strdup_printf("/ch%d", rtspServerData.ch);
    //__LOG(LOG_INFO, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

    gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

    gst_rtsp_media_factory_add_role(factory, cmdArg.rtsp_id,
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

    __LOG(LOG_INFO, "[RTSP][%s:%d]stream ready at rtsp://%s:*****@127.0.0.1:%s%s", _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_port, point);
    
    g_free(point);

    return ret;
}
#else
gboolean RtspServerBin::init(guint8 ch, gboolean crop_en) {
  gboolean ret = 0;
  const gboolean use_h265 = g_strcmp0(cmdArg.enc, ENC_H265) == 0;
  const gchar *parser_factory = use_h265 ? "h265parse" : "h264parse";
  const gchar *encoder_factory =
      use_h265 ? "vpuenc_hevc" : "vpuenc_h264";
  const gchar *encoded_media_type =
      use_h265 ? "video/x-h265" : "video/x-h264";
  const gchar *payloader_factory = use_h265 ? "rtph265pay" : "rtph264pay";
  rtspServerData.ch = ch;
  if (!rtspServerData.sync_trace)
    rtspServerData.sync_trace = rtsp_sync_trace_new(ch);
  // sinkPad = NULL;
  __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d, crop : %s", _FILE_, __LINE__,
        __FUNCTION__, rtspServerData.ch, crop_en ? "enable" : "disable");

  re.bin = gst_bin_new(g_strdup_printf("rtspServerBin%d", rtspServerData.ch));
  re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
  re.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
  re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
  re.capsfilter2 = gst_element_factory_make("capsfilter", "capsfilter2");
  re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
  re.convert2 = gst_element_factory_make("imxvideoconvert_g2d", "convert2");
  re.videoflip = gst_element_factory_make("videoflip", "videoflip");
  re.parse = gst_element_factory_make(parser_factory, parser_factory);
  re.enc = gst_element_factory_make(encoder_factory, encoder_factory);
  re.rate = gst_element_factory_make("videorate", "videorate");
  re.sink = gst_element_factory_make("appsink", "appsink");
  re.crop = gst_element_factory_make("videocrop", "crop");
  re.tee = gst_element_factory_make("tee", "tee");
  // re.overlay = gst_element_factory_make("textoverlay", "overlay");
  re.overlay = gst_element_factory_make("timeoverlay", "overlay");
  re.identity = gst_element_factory_make("identity", "identity");
  re.compositor = gst_element_factory_make("imxcompositor_g2d", "compositor");

  if (!re.bin || !re.queue || !re.capsfilter || !re.parse || !re.enc ||
      !re.rate || !re.sink || !re.convert || !re.queue2 || !re.crop ||
      !re.overlay || !re.compositor || !re.convert2 || !re.capsfilter2 ||
      !re.videoflip || !re.identity || !re.tee) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp element create error", _FILE_, __LINE__);
    return ret;
  }

  if (cmdArg.videorate_en)
    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.parse,
                     re.sink, re.capsfilter, re.crop, re.overlay, re.enc,
                     re.capsfilter2, NULL);
  else
    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.convert, re.parse,
                     re.sink, re.capsfilter, re.crop, re.overlay, re.enc,
                     re.capsfilter2, NULL);

  // gst_bin_add_many(GST_BIN(re.bin), re.convert2, re.compositor,
  // re.capsfilter2, re.videoflip, re.convert2, NULL);

  ret = gst_bin_add(GST_BIN(pipeline), re.bin);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp bin add error in pipeline", _FILE_,
          __LINE__);
    return ret;
  }

  GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                      //"format", G_TYPE_STRING, "NV12",
                                      //"width", G_TYPE_INT, cmdArg.width,
                                      //"height", G_TYPE_INT, cmdArg.height,
                                      "framerate", GST_TYPE_FRACTION,
                                      cmdArg.fps[STREAM_RTSP][ch], 1, NULL);

  g_object_set(re.capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  GstCaps *caps2 = gst_caps_new_empty_simple(encoded_media_type);
  g_object_set(re.capsfilter2, "caps", caps2, NULL);
  gst_caps_unref(caps2);

  // if(ch==1) g_object_set(re.convert, "rotation", 1, NULL);
  // if(ch==3) g_object_set(re.compositor, "rotate", 1, NULL);
  // if(ch==3) g_object_set(re.videoflip, "video-direction", 1, NULL);

  if (rtspServerData.ch % 2 == 0)
    g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right",
                 0, NULL);
  // g_object_set(re.crop, "top", 0, "bottom", 0, "left",
  // cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);
  else
    g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right",
                 cmdArg.width, NULL);
  // g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right",
  // cmdArg.res[cmdArg.resMode].width, NULL);

  g_object_set(re.overlay, "valignment", 2, NULL);
  g_object_set(re.overlay, "halignment", 0, NULL);
  g_object_set(re.overlay, "font-desc", DEFAULT_OVERLAY_FONT, NULL);

  g_object_set(re.enc, "bitrate", cmdArg.cam[ch].bps[STREAM_RTSP], NULL);
  g_object_set(re.enc, "gop-size", cmdArg.cam[ch].gop[STREAM_RTSP], NULL);
  /* quant is codec-specific; skip it cleanly when the selected encoder does
   * not expose the property. */
  enc_set_optional_int(re.enc, "quant", cmdArg.cam[ch].quant[STREAM_RTSP], ch);
  enc_set_optional_int(re.enc, "profile", cmdArg.cam[ch].profile[STREAM_RTSP], ch);
  enc_set_optional_int(re.enc, "qp-min", cmdArg.cam[ch].qp_min[STREAM_RTSP], ch);
  enc_set_optional_int(re.enc, "qp-max", cmdArg.cam[ch].qp_max[STREAM_RTSP], ch);
  const gint bin_queue_ms =
      clamp_int(cmdArg.rtsp_bin_queue_max_time_ms, 0, 2000);
  g_object_set(re.queue, "max-size-time", (guint64)bin_queue_ms * GST_MSECOND,
               "leaky", LEAKY_DOWNSTREAM, NULL);

#ifdef CHANNEL_EACH_CROP
  if (crop_en && cmdArg.overlay_en) {
    if (cmdArg.videorate_en)
      ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert,
                                  re.rate, re.capsfilter, re.enc, re.capsfilter2,
                                  re.parse, re.sink, NULL);
    else
      ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert,
                                  re.capsfilter, re.enc, re.capsfilter2,
                                  re.parse, re.sink, NULL);
  } else if (crop_en) {
    if (cmdArg.dual_enc == TRUE) {
      if (cmdArg.videorate_en)
        ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate,
                                    re.capsfilter, re.enc, re.capsfilter2,
                                    re.parse, re.sink, NULL);
      else
        ret = gst_element_link_many(re.queue, re.crop, re.convert,
                                    re.capsfilter, re.enc, re.capsfilter2,
                                    re.parse, re.sink, NULL);
    } else {
      gst_bin_remove_many(GST_BIN(re.bin), re.crop, re.convert,
                          re.capsfilter, re.enc, re.capsfilter2, re.parse,
                          NULL);
      if (re.rate) {
        gst_bin_remove(GST_BIN(re.bin), re.rate);
        re.rate = NULL;
      }
      re.crop = NULL;
      re.convert = NULL;
      re.capsfilter = NULL;
      re.enc = NULL;
      re.capsfilter2 = NULL;
      re.parse = NULL;
      ret = gst_element_link_many(re.queue, re.sink, NULL);
    }
  } else {
    if (cmdArg.dual_enc == TRUE) {
      if (cmdArg.videorate_en)
        ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc,
                                    re.capsfilter2, re.parse, re.sink, NULL);
      else
        ret = gst_element_link_many(re.queue, re.capsfilter, re.enc,
                                    re.capsfilter2, re.parse, re.sink, NULL);
    } else {
      gst_bin_remove_many(GST_BIN(re.bin), re.crop, re.convert,
                          re.capsfilter, re.enc, re.capsfilter2, re.parse,
                          NULL);
      if (re.rate) {
        gst_bin_remove(GST_BIN(re.bin), re.rate);
        re.rate = NULL;
      }
      re.crop = NULL;
      re.convert = NULL;
      re.capsfilter = NULL;
      re.enc = NULL;
      re.capsfilter2 = NULL;
      re.parse = NULL;
      ret = gst_element_link_many(re.queue, re.sink, NULL);
    }
  }

  // if(cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.crop,
  // re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.parse,
  // re.queue2, re.sink, NULL); else ret = gst_element_link_many(re.queue,
  // re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2,
  // re.sink, NULL); if(cmdArg.mode) ret = gst_element_link_many(re.queue,
  // re.crop, re.convert, re.enc, re.parse, re.queue2, re.sink, NULL);
#else
  ret =
      gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc,
                            re.capsfilter2, re.parse, re.queue2, re.sink, NULL);
#endif
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp link err", _FILE_, __LINE__);
    return ret;
  }

  /* videorate 없을 때 PTS 없는 버퍼 드롭 (dual_enc에서 enc가 있는 경우만) */
  if (!cmdArg.videorate_en && re.enc) {
    gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"),
                      GST_PAD_PROBE_TYPE_BUFFER,
                      (GstPadProbeCallback)drop_no_pts_probe_rtsp, NULL, NULL);
  }

  // g_object_set(re.convert, "composition-meta-enable", TRUE, NULL);
  // g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);

  // if(cmdArg.rtsp_fps >= 25) g_object_set(re.rate, "max-rate",
  // cmdArg.rtsp_fps, "drop-only", TRUE, NULL);

  // g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND,
  // "max-size-buffers", 60, "leaky", 1, NULL);
  const gint appsink_max_buffers =
      clamp_int(cmdArg.rtsp_appsink_max_buffers, 1, 120);
  g_object_set(re.sink, "max-buffers", appsink_max_buffers, NULL);
  g_object_set(re.sink, "drop", TRUE, NULL);
  // g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
  // g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
  g_object_set(re.sink, "emit-signals", TRUE, "sync", FALSE, "async", FALSE,
               NULL);
  // g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);

  g_signal_connect(re.sink, "eos", G_CALLBACK(eos_callback), &rtspServerData);
  g_signal_connect(re.sink, "new-sample", G_CALLBACK(new_sample_handler),
                   &rtspServerData);
  g_signal_connect(re.sink, "new-preroll", G_CALLBACK(new_preroll_handler),
                   &rtspServerData);
  // g_signal_connect(re.identity, "handoff", G_CALLBACK(on_identity_handoff),
  // &rtspServerData);

  sinkPad = gst_ghost_pad_new(
      g_strdup_printf("rtspServerBin_sink_ch%d", rtspServerData.ch),
      gst_element_get_static_pad(re.queue, "sink"));
  // sinkPad = gst_ghost_pad_new(g_strdup_printf("rtspServerBin_sink_ch%d",
  // rtspServerData.ch), gst_element_get_static_pad(re.sink, "sink"));
  ret = gst_element_add_pad(re.bin, sinkPad);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] rtsp pad add err", _FILE_, __LINE__);
    return ret;
  }

  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  // rtspServerData.appSrcName = g_strdup_printf("%s%d", "appsrc", ch);
  rtspServerData.appSrcName = g_strdup_printf("%s%d", "rtsp_appsrc", ch);

  // gchar *launch_str = g_strdup_printf("( appsrc name=%s ! queue
  // max-size-buffers=60 leaky=2 ! vpuenc_h264 bitrate=1024 ! h264parse
  // config-interval=-1 ! rtph264pay name=pay0 config-interval=-1 )",
  // rtspServerData.appSrcName);
  const gint q_max_buffers =
      clamp_int(cmdArg.rtsp_factory_queue_max_buffers, 1, 120);
  const gint appsrc_max_bytes =
      clamp_int(cmdArg.rtsp_appsrc_max_bytes, 16384, 2097152);
  gchar *launch_str = g_strdup_printf(
      "( appsrc name=%s do-timestamp=1 is-live=1 format=3 max-bytes=%d "
      "! queue name=rtsp_out_queue max-size-buffers=%d max-size-time=0 "
      "max-size-bytes=0 leaky=2 ! "
      "%s config-interval=-1 ! %s name=pay0 config-interval=-1 )",
      rtspServerData.appSrcName, appsrc_max_bytes, q_max_buffers,
      parser_factory, payloader_factory);
  /* 접속 시 강제 키프레임 요청 경로 (video 전용 — audio는 NULL 유지) */
  rtspServerData.kick_sink = re.sink;
  // gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1
  // is-live=1 block=true ! queue max-size-buffers=5 leaky=2
  // min-threshold-time=500000000 ! h264parse ! rtph264pay name=pay0
  // config-interval=0 )", rtspServerData.appSrcName);

  gst_rtsp_media_factory_set_launch(factory, launch_str);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  const gint factory_latency_ms =
      clamp_int(cmdArg.rtsp_factory_latency_ms, 1, 2000);
  gst_rtsp_media_factory_set_latency(factory, factory_latency_ms);
  g_free(launch_str);

  log_once(LOG_INFO,
           g_strdup_printf("[GST][%s:%d] eos shutdown : %s", _FILE_, __LINE__,
                           gst_rtsp_media_factory_is_eos_shutdown(factory)
                               ? "TRUE"
                               : "FALSE"));
  log_once(LOG_INFO,
           g_strdup_printf("[GST][%s:%d] latency : %d", _FILE_, __LINE__,
                           gst_rtsp_media_factory_get_latency(factory)));
  // rtspServerData.appsrc = gst_element_factory_make("appsrc",
  // rtspServerData.appSrcName);
  g_signal_connect(factory, "media-configure", (GCallback)media_configure,
                   &rtspServerData);

  gchar *point = g_strdup_printf("/ch%d", rtspServerData.ch);
  //__LOG(LOG_INFO, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

  gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

  gst_rtsp_media_factory_add_role(
      factory, cmdArg.rtsp_id, GST_RTSP_PERM_MEDIA_FACTORY_ACCESS,
      G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT,
      G_TYPE_BOOLEAN, TRUE, NULL);

  __LOG(LOG_INFO, "[RTSP][%s:%d]stream ready at rtsp://%s:*****@127.0.0.1:%s%s",
        _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_port, point);

  g_free(point);

  return ret;
}
#endif

#if 1
void rtspServerSendEosToAllAppsrc() {
  GstElement *targets[MAX_RTSP_APPSRC] = { NULL };
  guint8 count;

  /* lock 하에 ref 스냅샷을 떠서, unprepared의 unref와 경합해도 EOS 전송
   * 동안 객체가 살아있도록 한다 */
  g_mutex_lock(&g_rtsp_appsrc_lock);
  count = g_rtsp_appsrc_count;
  for (guint8 i = 0; i < count; i++) {
    if (g_rtsp_appsrc[i]) {
      targets[i] = (GstElement *)gst_object_ref(g_rtsp_appsrc[i]);
      g_rtsp_appsrc[i] = NULL;
    }
  }
  g_rtsp_appsrc_count = 0;
  g_mutex_unlock(&g_rtsp_appsrc_lock);

  for (guint8 i = 0; i < count; i++) {
    if (targets[i]) {
      __LOG(LOG_INFO, "[RTSP][%s:%d] sending EOS to appsrc ch%d", _FILE_, __LINE__, i);
      gst_app_src_end_of_stream(GST_APP_SRC(targets[i]));
      gst_object_unref(targets[i]);
    }
  }
}

static GstRTSPFilterResult
remove_all_sessions_cb(GstRTSPSessionPool *pool, GstRTSPSession *session, gpointer user_data) {
  (void)pool; (void)session; (void)user_data;
  return GST_RTSP_FILTER_REMOVE;
}

void rtspServerCloseAllSessions() {
  if (!rtspServer)
    return;

  GstRTSPSessionPool *pool = gst_rtsp_server_get_session_pool(rtspServer);
  if (!pool)
    return;

  GList *removed = gst_rtsp_session_pool_filter(pool, remove_all_sessions_cb, NULL);
  guint count = g_list_length(removed);
  g_list_free_full(removed, g_object_unref);
  __LOG(LOG_INFO, "[RTSP][%s:%d] force-closed %u RTSP sessions", _FILE_, __LINE__, count);
  g_object_unref(pool);
}

void rtspServerStop() {
  if (rtspServer) {
    __LOG(LOG_INFO, "[RTSP][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    g_object_unref(rtspServer);
    rtspServer = NULL;
  }

  if (cleanSesson_id)
    g_source_remove(cleanSesson_id);
  if (removeSesson_id)
    g_source_remove(removeSesson_id);
}

guint rtspServerStart() {
  if (rtspServer)
    return 1;

  __LOG(LOG_INFO, "[RTSP][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  rtspServer = gst_rtsp_server_new();
  g_object_set(rtspServer, "service", cmdArg.rtsp_port, NULL);

  rtspMounts = gst_rtsp_server_get_mount_points(rtspServer);

  // GstRTSPSessionPool *session_pool =
  // gst_rtsp_server_get_session_pool(rtspServer); g_object_set(session_pool,
  // "max-sessions", 10, NULL); g_object_set(session_pool, "timeout", 60, NULL);
  // gst_object_unref(session_pool);

  // cleanSesson_id = g_timeout_add_seconds (DEFAULT_RTSP_SESSION_CLEAN_PERIOD,
  // (GSourceFunc) cleanRtspSession, rtspServer);
  cleanSesson_id = g_timeout_add_seconds(
      cmdArg.duration * 60, (GSourceFunc)cleanRtspSession, rtspServer);
  // removeSesson_id = g_timeout_add_seconds (60, (GSourceFunc) remove_sessions,
  // rtspServer);

  g_signal_connect(rtspServer, "client-connected",
                   G_CALLBACK(handle_client_connected), NULL);

  /* make a new authentication manager */
  GstRTSPAuth *auth = gst_rtsp_auth_new();

  /* make user token */
  __LOG(LOG_INFO, "[RTSP][%s:%d] rtsp id : %s, passwd : *****", _FILE_,
        __LINE__, cmdArg.rtsp_id);
  GstRTSPToken *token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE,
                                           G_TYPE_STRING, cmdArg.rtsp_id, NULL);
  gchar *basic = gst_rtsp_auth_make_basic(cmdArg.rtsp_id, cmdArg.rtsp_passwd);
  gst_rtsp_auth_add_basic(auth, basic, token);
  g_free(basic);
  gst_rtsp_token_unref(token);
  gst_rtsp_server_set_auth(rtspServer, auth);
  g_object_unref(auth);
  // g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);

  return gst_rtsp_server_attach(rtspServer, NULL);
}
#endif
