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

GstRTSPMountPoints *rtspMounts = NULL;
GstRTSPServer *rtspServer = NULL;
guint cleanRtsp_id = 0;

static void enough_data(GstElement *source, gpointer user_data) 
{
    RtspServerData *info = (RtspServerData*)user_data;
	__LOG(LOG_ERR, "[RTSP][%s:%d] ch %d Enough Data !!!", _FILE_, __LINE__, info->ch);
}

static gboolean cleanRtspSession(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;

   //__LOG(LOG_DEBUG, "[GST][%s:%d] rtsp session pool", _FILE_, __LINE__);

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

static void client_closed(GstRTSPClient* client, gpointer user_data)
{
	//CustomData *info = (CustomData*)user_data;
    //gchar *client_ip = (gchar *)user_data;
	const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP2 : %s", _FILE_, __LINE__, client_ip_2);

    //if(client_ip)g_free(client_ip);
}

static gboolean handle_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) 
{
    //CustomData *info = (CustomData*)user_data;
    // This function is called when a new client is connected to the RTSP server.
    // You can handle the client connection here.
    const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));
    //GstRTSPUrl *url = gst_rtsp_connection_get_url(gst_rtsp_client_get_connection(client));
    //GstRTSPMountPoints *mount = gst_rtsp_client_get_mount_points(client);
    //GstRTSPContext  *path = gst_rtsp_client_get_rtsp_context(client);
    //GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->host);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->abspath);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %d", _FILE_, __LINE__, url->port);

    g_signal_connect(client, "closed", (GCallback)(client_closed), NULL);

    //g_free(client_ip);
    // Initialize session for the new client
    
    return TRUE;
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb (GstRTSPMedia * media)
{
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams (media);

  //GST_INFO ("media %p is prepared and has %u streams", media, n_streams);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] media %p is prepared and has %u streams", _FILE_, __LINE__, media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession (stream);
    //GST_INFO ("watching session %p on stream %u", session, i);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] watching session %p on stream %u", _FILE_, __LINE__, session, i);

    //g_signal_connect (session, "on-ssrc-active", (GCallback) on_ssrc_active, media);
    //g_signal_connect (session, "on-sender-ssrc-active", (GCallback) on_sender_ssrc_active, media);
  }
}

static void	media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
	//GstElement *src = (GstElement*)user_data;
    RtspServerData *info = (RtspServerData*)user_data;
	GstElement *element;
    //GstElement *queue;
    //gchar *queue_name;
    //static GMutex mutex;

	//g_mutex_lock(&mutex);
    __LOG(LOG_NOTICE, "[GST][%s:%d] media connect ch:%d", _FILE_, __LINE__, __FUNCTION__, info->ch);
	element = gst_rtsp_media_get_element(media);
    //name = GST_ELEMENT_NAME(GST_APP_SRC(element));
    //name = gst_object_get_name(GST_OBJECT(element));
    __LOG(LOG_INFO, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, info->appSrcName);
	info->appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), info->appSrcName);
    
    //queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	//queue = gst_bin_get_by_name_recurse_up(GST_BIN(element), queue_name);

    //g_object_set (G_OBJECT (queue), 
    //g_free(queue_name);
	gst_object_unref(element);

    g_signal_connect(media, "prepared", (GCallback) media_prepared_cb, factory);
    g_signal_connect(info->appsrc, "enough-data", (GCallback) enough_data, info);
	//g_mutex_unlock(&mutex);

#ifdef DYNAMIC_CAPS
    GstCaps *caps;

    g_object_get(info->appsrc, "caps", &caps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, info->ch, gst_caps_to_string(caps));
    g_object_set(info->appsrc, "caps", info->caps, NULL);
    g_object_get(info->appsrc, "caps", &caps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set caps : %s", _FILE_, __LINE__, info->ch, gst_caps_to_string(caps));

    gst_caps_unref(caps);
#endif

    return;
}

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static GstFlowReturn new_sample_handler(GstElement *sink, gpointer userData) 
{
    
    GstSample *sample;
    GstBuffer *buffer;
    RtspServerData *info = (RtspServerData *)userData;
    GstMapInfo map;

    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
#ifdef DYNAMIC_CAPS
    if(!info->caps) {
        info->caps = gst_pad_get_current_caps(gst_element_get_static_pad(sink, "sink"));
        //__LOG(LOG_NOTICE, "[GST][%s:%d] ch %d caps : %s", _FILE_, __LINE__, __FUNCTION__, info->ch, gst_caps_to_string(info->caps));
        //g_print("ch%d caps : %s\n", info->ch, gst_caps_to_string(info->caps));
    }
#endif

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }
    buffer = gst_sample_get_buffer(sample);
    gst_sample_unref(sample);
    if(info->debug)
    {
        GstClockTime timestamp = GST_BUFFER_PTS(buffer);
        g_message("Timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(timestamp));
        info->debug = 0;
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
    if(info->appsrc == NULL)
    {
        //g_print("appsrc null return!\n");
        return GST_FLOW_OK;
    }
#endif


    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        //gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        g_printerr("Failed to map buffer\n");
        return GST_FLOW_ERROR;
    }

    // Create a new buffer and copy data
    info->buf = gst_buffer_new_and_alloc(map.size);
    gst_buffer_fill(info->buf, 0, map.data, map.size);

    // Unmap the original buffer
    // gst_buffer_unmap(buffer, &map);
    // info->buf = newBuffer;

    // Push the new buffer to the appsrc
    // g_thread_new("data-processing-thread", (GThreadFunc)process_data_thread, info);
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);

    gst_buffer_unmap(buffer, &map);
    //gst_sample_unref(sample);

    return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) 
{
    RtspServerData *info = (RtspServerData *)data;
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, info->ch);
    info->start_f = TRUE;

    return GST_FLOW_OK;
}

gboolean RtspServerBin::getStartFlag()
{
    return rtspServerData.start_f;
}

void RtspServerBin::setOverlayText(gchar *text)
{
    g_object_set(re.overlay, "text", text, NULL);
}

void RtspServerBin::setTimeStampDebug()
{
    rtspServerData.debug = TRUE;
}

void RtspServerBin::getBitrate()
{
    gint bps;

    g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_print("rtsp ch%d get bitrate : %d\n", ch, bps);
}

void RtspServerBin::setBitrate(guint16 data)
{
    gint bps;

    g_object_set(re.enc, "bitrate", data, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_print("rtsp ch%d set bitrate : %d\n", ch, bps);
    rtspServerData.caps = NULL;
}

void RtspServerBin::getFps()
{
    //gint fps;
    GstCaps *caps;

    g_object_get(re.capsfilter, "caps", &caps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get fps : %s", _FILE_, __LINE__, ch, gst_caps_to_string(caps));
    g_print("rtsp ch%d get fps : %s\n", ch, gst_caps_to_string(caps));

    gst_caps_unref(caps);
}

void RtspServerBin::setFps(guint16 data)
{
    //gint fps;
    GstCaps *caps;

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
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set fps : %s", _FILE_, __LINE__, ch, gst_caps_to_string(caps));
    g_print("rtsp ch%d set fps : %s\n", ch, gst_caps_to_string(caps));
    rtspServerData.caps = NULL;

    gst_caps_unref(caps);
}

void RtspServerBin::getCaps()
{
    GstCaps *caps;

    if(rtspServerData.appsrc == NULL) return;

    g_object_get(rtspServerData.appsrc, "caps", &caps, NULL);
    //g_object_get(info->appsrc, "caps", &caps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch, gst_caps_to_string(caps));
    g_print("rtsp ch%d get caps : %s\n", ch, gst_caps_to_string(caps));
    gst_caps_unref(caps);
}

void RtspServerBin::setRotation(guint16 data)
{
    gint rotation;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.convert, "rotation", data, NULL);
    g_object_get(re.convert, "rotation", &rotation, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set rotation : %d", _FILE_, __LINE__, ch, rotation);
    g_print("rtsp ch%d set rotation : %d\n", ch, rotation);
}

void RtspServerBin::getRotation()
{
    gint rotation;

    g_object_get(re.convert, "rotation", &rotation, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get rotation : %d", _FILE_, __LINE__, ch, rotation);
    g_print("rtsp ch%d get rotation : %d\n", ch, rotation);
}

void RtspServerBin::setGop(guint16 data)
{
    gint gop;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "gop-size", data, NULL);
    g_object_get(re.enc, "gop-size", &gop, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set gop_size : %d", _FILE_, __LINE__, ch, gop);
    g_print("rtsp ch%d set gop_size : %d\n", ch, gop);
}

void RtspServerBin::getGop()
{
    gint gop;

    g_object_get(re.enc, "gop-size", &gop, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get gop_size : %d", _FILE_, __LINE__, ch, gop);
    g_print("rtsp ch%d get gop_size : %d\n", ch, gop);
}

void RtspServerBin::getKeyframe()
{
    gint key;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get keyframe : %d", _FILE_, __LINE__, ch, key);
    g_print("rtsp ch%d get keyframe : %d\n", ch, key);
}

void RtspServerBin::setkeyframe(guint16 data)
{
    gint key;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "set-keyframe", data, NULL);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set keyframe : %d", _FILE_, __LINE__, ch, key);
    g_print("rtsp ch%d set keyframe : %d\n", ch, key);
}

RtspServerBin* RtspServerBin::getInstance()
{
	static RtspServerBin instance;
	return &instance;
}

RtspServerBin::RtspServerBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    sinkPad = NULL;
    re.bin = NULL;
    rtspServerData.caps = NULL;
    rtspServerData.appsrc = NULL;
    rtspServerData.start_f = FALSE;
    rtspServerData.debug = FALSE;
}

RtspServerBin::~RtspServerBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
}

GstPad* RtspServerBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gint RtspServerBin::init(guint8 num, gboolean crop_en)
{
    GstPad *staticPad;
    gint ret = 0;
    ch = num;
    rtspServerData.ch = ch;
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, ch);

    re.bin = gst_bin_new(g_strdup_printf("rtspServerBin%d", ch));
    re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    re.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    re.convert2 = gst_element_factory_make("imxvideoconvert_g2d", "convert2");
    re.parse = gst_element_factory_make("h264parse", "h264parse");
    re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.sink = gst_element_factory_make("appsink", "appsink");
    re.crop = gst_element_factory_make("videocrop", "crop");
    re.overlay = gst_element_factory_make("textoverlay", "overlay");
    re.compositor = gst_element_factory_make("imxcompositor_g2d", "compositor");

    if (!re.bin || !re.queue || !re.capsfilter || !re.parse || !re.enc || !re.rate || !re.sink || !re.convert || !re.queue2 || !re.crop \
        || !re.overlay || !re.convert2 || !re.compositor) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.enc, re.parse, re.queue2, re.sink, re.capsfilter, re.crop, re.overlay, \
        re.convert2, re.compositor, NULL);

    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

#ifdef CHANNEL_EACH_CROP
    if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
    else if(cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
    else if(crop_en) ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
    else ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);

    //if(cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
    //else ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
    //if(cmdArg.mode) ret = gst_element_link_many(re.queue, re.crop, re.convert, re.enc, re.parse, re.queue2, re.sink, NULL);
#else
    ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL);
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp link err", _FILE_, __LINE__);
        return ret;
    }

    //g_object_set(re.convert, "composition-meta-enable", TRUE, NULL);
    //g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, cmdArg.fps[STREAM_RTSP], 1, NULL);
    g_object_set(re.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (ch % 2 == 0)
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
        //g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);
    else
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);
        //g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);

    //if(cmdArg.rtsp_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rtsp_fps, "drop-only", TRUE, NULL);
    g_object_set(re.overlay, "valignment", 2, NULL);
    g_object_set(re.overlay, "halignment", 0, NULL);
    g_object_set(re.overlay, "font-desc", DEFAULT_OVERLAY_FONT, NULL);

    g_object_set(re.enc, "bitrate", cmdArg.bps[STREAM_RTSP], NULL);
    g_object_set(re.enc, "gop-size", cmdArg.gop[STREAM_RTSP], NULL);
    g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.fps[STREAM_RTSP], "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(re.queue2, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.fps[STREAM_RTSP], "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(re.sink, "max-buffers", cmdArg.fps[STREAM_RTSP], NULL);
    g_object_set(re.sink, "drop", TRUE, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(re.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(re.sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(re.sink, "new-sample", G_CALLBACK(new_sample_handler), &rtspServerData);
    g_signal_connect(re.sink, "new-preroll", G_CALLBACK(new_preroll_handler), &rtspServerData);

    staticPad = gst_element_get_static_pad(re.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("rtspServerBin_sink_ch%d", ch), staticPad);

    ret = gst_element_add_pad(re.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp pad add err", _FILE_, __LINE__);
        return ret;
    }

    gst_object_unref(staticPad);

    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    //rtspServerData.appSrcName = g_strdup_printf("%s%d", "appsrc", ch);
    rtspServerData.appSrcName = g_strdup_printf("%s", "appsrc");

    gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=1 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", rtspServerData.appSrcName);
    
    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    g_free(launch_str);
    
    log_once(LOG_INFO, g_strdup_printf("[GST][%s:%d] eos shutdown : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_eos_shutdown(factory)? "TRUE":"FALSE"));
    log_once(LOG_INFO, g_strdup_printf("[GST][%s:%d] latency : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_latency(factory)));
    //rtspServerData.appsrc = gst_element_factory_make("appsrc", rtspServerData.appSrcName);
    g_signal_connect(factory, "media-configure", (GCallback)media_configure, &rtspServerData);

    gchar *point = g_strdup_printf("/ch%d", ch);
    //__LOG(LOG_INFO, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

    gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

    gst_rtsp_media_factory_add_role(factory, cmdArg.rtsp_id,
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

    __LOG(LOG_INFO, "[RTSP][%s:%d]stream ready at rtsp://%s:%s@127.0.0.1:%s%s", _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_passwd, cmdArg.rtsp_port, point);
    
    g_free(point);

    return ret;
}

#if 1
void rtspServerStop()
{
    if(rtspServer) g_object_unref(rtspServer);
    if(cleanRtsp_id) g_source_remove(cleanRtsp_id);
}

gint rtspServerStart()
{
    gint ret = 0;
    if(rtspServer) return ret;

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    rtspServer = gst_rtsp_server_new ();
    g_object_set (rtspServer, "service", cmdArg.rtsp_port, NULL);

    rtspMounts = gst_rtsp_server_get_mount_points (rtspServer);

    cleanRtsp_id = g_timeout_add_seconds (5, (GSourceFunc) cleanRtspSession, rtspServer);

    g_signal_connect(rtspServer, "client-connected", G_CALLBACK(handle_client_connected), NULL);

    /* make a new authentication manager */
    GstRTSPAuth *auth = gst_rtsp_auth_new ();
    
    /* make user token */
    __LOG(LOG_INFO, "[RTSP][%s:%d] rtsp id : %s, passwd : %s", _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_passwd);
    GstRTSPToken *token = gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, cmdArg.rtsp_id, NULL);
    gchar *basic = gst_rtsp_auth_make_basic (cmdArg.rtsp_id, cmdArg.rtsp_passwd);
    gst_rtsp_auth_add_basic (auth, basic, token);
    g_free (basic);
    gst_rtsp_token_unref (token);
    gst_rtsp_server_set_auth (rtspServer, auth);
    g_object_unref (auth);
    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);

    ret = gst_rtsp_server_attach (rtspServer, NULL);
    if (!ret) {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_, __LINE__);
        return ret;
    }

    return ret;
}
#endif