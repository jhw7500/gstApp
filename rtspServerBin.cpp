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
#include <gst/rtsp-server/rtsp-server.h>

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

static gboolean link_with_notice(GstElement *src, GstElement *sink,
                                 const gchar *src_name,
                                 const gchar *sink_name,
                                 guint8 ch) {
  GstPad *src_pad = NULL;
  GstPad *sink_pad = NULL;
  GstCaps *src_caps = NULL;
  GstCaps *sink_caps = NULL;
  gchar *src_caps_str = NULL;
  gchar *sink_caps_str = NULL;
  gboolean ret = FALSE;

  src_pad = gst_element_get_static_pad(src, "src");
  sink_pad = gst_element_get_static_pad(sink, "sink");

  if (src_pad)
    src_caps = gst_pad_query_caps(src_pad, NULL);
  if (sink_pad)
    sink_caps = gst_pad_query_caps(sink_pad, NULL);

  if (src_caps)
    src_caps_str = gst_caps_to_string(src_caps);
  if (sink_caps)
    sink_caps_str = gst_caps_to_string(sink_caps);

  __LOG(LOG_NOTICE,
        "[GST][%s:%d] ch%d rtsp raw try %s -> %s src_linked=%d sink_linked=%d src_caps=%s sink_caps=%s",
        _FILE_, __LINE__, ch, src_name, sink_name,
        src_pad ? gst_pad_is_linked(src_pad) : -1,
        sink_pad ? gst_pad_is_linked(sink_pad) : -1,
        src_caps_str ? src_caps_str : "(null)",
        sink_caps_str ? sink_caps_str : "(null)");

  ret = gst_element_link(src, sink);

  __LOG(ret ? LOG_NOTICE : LOG_CRIT,
        "[GST][%s:%d] ch%d rtsp raw link %s -> %s : %s", _FILE_, __LINE__,
        ch, src_name, sink_name, ret ? "ok" : "fail");

  if (!ret) {
    GstPadLinkReturn pad_ret = GST_PAD_LINK_OK;

    if (src_pad && sink_pad) {
      pad_ret = gst_pad_link(src_pad, sink_pad);
      __LOG(LOG_NOTICE,
            "[GST][%s:%d] ch%d rtsp raw pad link %s -> %s : %s", _FILE_,
            __LINE__, ch, src_name, sink_name, gst_pad_link_get_name(pad_ret));
      if (pad_ret == GST_PAD_LINK_OK)
        gst_pad_unlink(src_pad, sink_pad);
    }
  }

  if (src_caps_str)
    g_free(src_caps_str);
  if (sink_caps_str)
    g_free(sink_caps_str);
  if (src_caps)
    gst_caps_unref(src_caps);
  if (sink_caps)
    gst_caps_unref(sink_caps);
  if (src_pad)
    gst_object_unref(src_pad);
  if (sink_pad)
    gst_object_unref(sink_pad);

  return ret;
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
  __LOG(LOG_ERR, "[RTSP][%s:%d] ch %d Enough Data !!!", _FILE_, __LINE__,
        info->ch);
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
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] ch%d media unprepared — clearing appsrc",
        _FILE_, __LINE__, info->ch);
  /* 전역 추적 배열에서 제거 */
  if (info->ch < MAX_RTSP_APPSRC)
    g_rtsp_appsrc[info->ch] = NULL;
  if (info->appsrc) {
    gst_object_unref(info->appsrc);
    info->appsrc = NULL;
  }
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
  info->appsrc =
      gst_bin_get_by_name_recurse_up(GST_BIN(element), info->appSrcName);
  if (info->appsrc == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] appsrc is null", _FILE_, __LINE__);
    goto media_configure_out;
  }
  /* 전역 추적 배열에 등록 (종료 시 EOS 전송용) */
  if (info->ch < MAX_RTSP_APPSRC) {
    g_rtsp_appsrc[info->ch] = info->appsrc;
    if (info->ch >= g_rtsp_appsrc_count)
      g_rtsp_appsrc_count = info->ch + 1;
  }
  if (info->caps)
    g_object_set(info->appsrc, "caps", info->caps, NULL);
  else {
    GstCaps *fallback_caps = NULL;

    if (cmdArg.rtsp_raw)
      fallback_caps = gst_caps_from_string("video/x-raw,format=I420");
    else
      fallback_caps = gst_caps_from_string(
          "video/x-h264,stream-format=byte-stream,alignment=au");

    if (fallback_caps) {
      g_object_set(info->appsrc, "caps", fallback_caps, NULL);
      gst_caps_unref(fallback_caps);
    }
  }

  {
    GstCaps *appsrc_caps = NULL;
    gchar *caps_str = NULL;

    g_object_get(info->appsrc, "caps", &appsrc_caps, NULL);
    if (appsrc_caps)
      caps_str = gst_caps_to_string(appsrc_caps);

    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d appsrc caps : %s", _FILE_, __LINE__,
          info->ch, caps_str ? caps_str : "(null)");

    if (caps_str)
      g_free(caps_str);
    if (appsrc_caps)
      gst_caps_unref(appsrc_caps);
  }
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
  if (info->appsrc)
    g_signal_connect(info->appsrc, "enough-data", (GCallback)enough_data,
                     info);

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
  // gst_sample_unref(sample);

  sample_caps = gst_sample_get_caps(sample);
  if (sample_caps) {
    if (!info->caps || !gst_caps_is_equal(info->caps, sample_caps)) {
      if (info->caps)
        gst_caps_unref(info->caps);
      info->caps = gst_caps_copy(sample_caps);
    }
  }

#if 0
    GstFlowReturn ret;
    g_signal_emit_by_name(info->appsrc, "push-buffer", buffer, &ret);
    if(ret != GST_FLOW_OK)
    {
        g_print("Failed to push buffer\n");
    }
#endif

#if 1
  if (info->appsrc == NULL || is_interrupted) {
    // if(info->ch == 0) g_print("ch%d appsrc null return!\n", info->ch);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }
#endif

  if (info->caps)
    g_object_set(info->appsrc, "caps", info->caps, NULL);

  if (!buffer) {
    __LOG(LOG_CRIT, "[RTSP][%s:%d] ch%d buffer cannot get from sample", _FILE_,
          __LINE__, info->ch);
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

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

  // Push the buffer (takes ownership)
  GstFlowReturn push_ret =
      gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), out_buffer);
  if (push_ret != GST_FLOW_OK) {
    __LOG(LOG_WARNING, "[RTSP][%s:%d] ch%d appsrc push failed: %d", _FILE_,
          __LINE__, info->ch, push_ret);
  }

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

void RtspServerBin::setBitrate(guint16 data) {
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
  rtspServerData.caps = NULL;
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

  if (rtspServerData.appsrc == NULL)
    return;

  g_object_get(rtspServerData.appsrc, "caps", &caps, NULL);
  // g_object_get(info->appsrc, "caps", &caps, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch,
  //gst_caps_to_string(caps));
  g_print("rtsp ch%d get caps : %s\n", rtspServerData.ch,
          gst_caps_to_string(caps));
  gst_caps_unref(caps);
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
}

RtspServerBin::~RtspServerBin() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        rtspServerData.ch);
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
      "appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue "
      "max-size-buffers=%d max-size-time=0 max-size-bytes=0 leaky=2 ! "
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
    gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=%d leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", rtspServerData.appSrcName, cmdArg.fps[STREAM_RTSP][ch]);
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
  rtspServerData.ch = ch;
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
  re.parse = gst_element_factory_make("h264parse", "h264parse");
  re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
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

  GstCaps *caps = NULL;

  if (cmdArg.rtsp_raw)
    caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                               "I420", "framerate", GST_TYPE_FRACTION,
                               cmdArg.fps[STREAM_RTSP][ch], 1, NULL);
  else
    caps = gst_caps_new_simple("video/x-raw",
                               //"format", G_TYPE_STRING, "NV12",
                               //"width", G_TYPE_INT, cmdArg.width,
                               //"height", G_TYPE_INT, cmdArg.height,
                               "framerate", GST_TYPE_FRACTION,
                               cmdArg.fps[STREAM_RTSP][ch], 1, NULL);

  g_object_set(re.capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  GstCaps *caps2 = gst_caps_new_simple("video/x-h264", NULL);
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
  const gint bin_queue_ms =
      clamp_int(cmdArg.rtsp_bin_queue_max_time_ms, 0, 2000);
  g_object_set(re.queue, "max-size-time", (guint64)bin_queue_ms * GST_MSECOND,
               "leaky", LEAKY_DOWNSTREAM, NULL);

  __LOG(LOG_NOTICE,
        "[GST][%s:%d] ch%d rtsp path flags raw=%d crop=%d overlay=%d videorate=%d dual_enc=%d",
        _FILE_, __LINE__, ch, cmdArg.rtsp_raw, crop_en, cmdArg.overlay_en,
        cmdArg.videorate_en, cmdArg.dual_enc);

#ifdef CHANNEL_EACH_CROP
  if (crop_en && cmdArg.overlay_en) {
    if (cmdArg.rtsp_raw) {
      if (cmdArg.videorate_en) {
        ret = link_with_notice(re.queue, re.crop, "queue", "crop", ch) &&
              link_with_notice(re.crop, re.overlay, "crop", "overlay", ch) &&
              link_with_notice(re.overlay, re.convert, "overlay", "convert", ch) &&
              link_with_notice(re.convert, re.rate, "convert", "rate", ch) &&
              link_with_notice(re.rate, re.capsfilter, "rate", "capsfilter", ch) &&
              link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
      } else {
        ret = link_with_notice(re.queue, re.crop, "queue", "crop", ch) &&
              link_with_notice(re.crop, re.overlay, "crop", "overlay", ch) &&
              link_with_notice(re.overlay, re.convert, "overlay", "convert", ch) &&
              link_with_notice(re.convert, re.capsfilter, "convert", "capsfilter", ch) &&
              link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
      }
    } else if (cmdArg.videorate_en)
      ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert,
                                  re.rate, re.capsfilter, re.enc, re.capsfilter2,
                                  re.parse, re.sink, NULL);
    else
      ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert,
                                  re.capsfilter, re.enc, re.capsfilter2,
                                  re.parse, re.sink, NULL);
  } else if (crop_en) {
    if (cmdArg.dual_enc == TRUE) {
      if (cmdArg.rtsp_raw) {
        if (cmdArg.videorate_en) {
          ret = link_with_notice(re.queue, re.crop, "queue", "crop", ch) &&
                link_with_notice(re.crop, re.convert, "crop", "convert", ch) &&
                link_with_notice(re.convert, re.rate, "convert", "rate", ch) &&
                link_with_notice(re.rate, re.capsfilter, "rate", "capsfilter", ch) &&
                link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
        } else {
          ret = link_with_notice(re.queue, re.crop, "queue", "crop", ch) &&
                link_with_notice(re.crop, re.convert, "crop", "convert", ch) &&
                link_with_notice(re.convert, re.capsfilter, "convert", "capsfilter", ch) &&
                link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
        }
      } else if (cmdArg.videorate_en)
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
      if (cmdArg.rtsp_raw) {
        if (cmdArg.videorate_en) {
          ret = link_with_notice(re.queue, re.rate, "queue", "rate", ch) &&
                link_with_notice(re.rate, re.capsfilter, "rate", "capsfilter", ch) &&
                link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
        } else {
          ret = link_with_notice(re.queue, re.capsfilter, "queue", "capsfilter", ch) &&
                link_with_notice(re.capsfilter, re.sink, "capsfilter", "appsink", ch);
        }
      } else if (cmdArg.videorate_en)
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
  if (!cmdArg.videorate_en && re.enc && !cmdArg.rtsp_raw) {
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
  gchar *launch_str = NULL;

  if (cmdArg.rtsp_raw)
    launch_str = g_strdup_printf(
        "( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue "
        "max-size-buffers=%d max-size-time=0 max-size-bytes=0 leaky=2 ! "
        "rtpvrawpay name=pay0 pt=96 )",
        rtspServerData.appSrcName, q_max_buffers);
  else
    launch_str = g_strdup_printf(
        "( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue "
        "max-size-buffers=%d max-size-time=0 max-size-bytes=0 leaky=2 ! "
        "h264parse config-interval=-1 ! rtph264pay name=pay0 config-interval=-1 )",
        rtspServerData.appSrcName, q_max_buffers);
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
  for (guint8 i = 0; i < g_rtsp_appsrc_count; i++) {
    if (g_rtsp_appsrc[i]) {
      __LOG(LOG_INFO, "[RTSP][%s:%d] sending EOS to appsrc ch%d", _FILE_, __LINE__, i);
      gst_app_src_end_of_stream(GST_APP_SRC(g_rtsp_appsrc[i]));
      g_rtsp_appsrc[i] = NULL;
    }
  }
  g_rtsp_appsrc_count = 0;
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
