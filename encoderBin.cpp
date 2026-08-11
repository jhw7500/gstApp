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
    guint64 q_in;          /* 큐 sink 도착 프레임 */
    guint64 q_out;         /* 큐 src 통과 프레임 (q_in - q_out = leaky 드롭) */
    guint64 enc_out;       /* 인코더 출력 프레임 */
    guint64 overrun;       /* 큐 포화 횟수 (leaky 여도 발생) */
    guint lvl_buf_max;     /* current-level-buffers 워터마크 = 업스트림 풀 상한 단서 */
    gint64 enc_last_us;    /* 직전 인코더 출력 시각 (monotonic) */
    gint64 enc_gap_max_us; /* 인코더 출력 최대 간격 = VPU 스톨 지표 */
    gboolean active;
} EncStat;

static EncStat g_encStat[MAX_CHANNEL];

#define ENC_NODATA_WARN_SEC     5   /* 무데이터 감시 주기 */
#define ENC_NODATA_STRIKES      2   /* 다른 채널이 흐르는 중이면 10초만에 경고 */
#define ENC_NODATA_COLD_STRIKES 12  /* 전 채널 무데이터(기동 중)면 60초 유예 */

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
    s->q_in++;
    /* 큐가 가장 찬 시점에 근접한 샘플 (삽입 직전이라 실제보다 1 작을 수 있음) */
    if (cmdArg.queue_enc_stat_sec > 0 && s->queue) {
        guint lvl = 0;
        g_object_get(s->queue, "current-level-buffers", &lvl, NULL);
        if (lvl > s->lvl_buf_max)
            s->lvl_buf_max = lvl;
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

    if (!any_data_seen) {
        for (gint i = 0; i < MAX_CHANNEL; i++) {
            if (g_encStat[i].q_in > 0) {
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
            prev[i] = s->q_in;
            continue;
        }

        guint64 cur = s->q_in;
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
    g_encStat[GPOINTER_TO_INT(user_data)].q_out++;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
enc_out_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    EncStat *s = &g_encStat[GPOINTER_TO_INT(user_data)];
    gint64 now = g_get_monotonic_time();
    s->enc_out++;
    if (s->enc_last_us) {
        gint64 gap = now - s->enc_last_us;
        if (gap > s->enc_gap_max_us)
            s->enc_gap_max_us = gap;
    }
    s->enc_last_us = now;
    return GST_PAD_PROBE_OK;
}

static void enc_queue_overrun_cb(GstElement *queue, gpointer user_data)
{
    g_encStat[GPOINTER_TO_INT(user_data)].overrun++;
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

        guint64 d_in = s->q_in - prev_in[i];
        guint64 d_out = s->q_out - prev_out[i];
        guint64 d_enc = s->enc_out - prev_enc[i];
        guint64 d_over = s->overrun - prev_over[i];
        /* q_in/q_out 을 원자적으로 읽지 않으므로 그 사이에 버퍼가 통과하면
         * out 이 in 을 한 개 앞설 수 있다. 음수 leak 은 0 으로 본다. */
        gint64 d_leak = (gint64)d_in - (gint64)d_out;
        if (d_leak < 0)
            d_leak = 0;
        prev_in[i] = s->q_in;
        prev_out[i] = s->q_out;
        prev_enc[i] = s->enc_out;
        prev_over[i] = s->overrun;

        __LOG(LOG_NOTICE,
              "[GST][%s:%d] ch%d enc-stat[%ds] in=%" G_GUINT64_FORMAT "(%.1ffps)"
              " out=%" G_GUINT64_FORMAT " leak=%" G_GINT64_FORMAT
              " enc=%" G_GUINT64_FORMAT "(%.1ffps) overrun=%" G_GUINT64_FORMAT
              " lvl=%u buf/%u B (max %u buf) max-bytes=%u enc_gap_max=%.1fms",
              _FILE_, __LINE__, i, period,
              d_in, (gdouble)d_in / period,
              d_out, d_leak,
              d_enc, (gdouble)d_enc / period,
              d_over, lvl_buf, lvl_byte, s->lvl_buf_max, max_bytes,
              s->enc_gap_max_us / 1000.0);

        s->enc_gap_max_us = 0;  /* 구간별 최대값으로 재시작 */
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
