/*
 *
 * Cantops rtspServerBin.cpp support
 *
 * Copyright (C)2022 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 *    This program is free software; you can redistribute  it and/or modify it
 *    under  the terms of  the GNU General  Public License as published by the
 *    Free Software Foundation;  either version 2 of the  License, or (at your
 *    option) any later version.
 */

#include "rtspServerBin.h"

GstRTSPMountPoints *rtspMounts;
GstRTSPServer *rtspServer;

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
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
	element = gst_rtsp_media_get_element(media);
    //name = GST_ELEMENT_NAME(GST_APP_SRC(element));
    //name = gst_object_get_name(GST_OBJECT(element));
    __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, info->appSrcName);
	info->appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), info->appSrcName);

    //queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	//queue = gst_bin_get_by_name_recurse_up(GST_BIN(element), queue_name);

    //g_object_set (G_OBJECT (queue), 
    //g_free(queue_name);
	gst_object_unref(element);

    g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
	//g_mutex_unlock(&mutex);
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
    guint8 mode;
    GstMapInfo map;
    gchar *path = NULL;
    FILE *file;


    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }
    buffer = gst_sample_get_buffer(sample);
    gst_sample_unref(sample);

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
    if(path!=NULL) g_free(path);

    return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) 
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    return GST_FLOW_OK;
}

RtspServerBin* RtspServerBin::getInstance()
{
	static RtspServerBin instance;
	return &instance;
}

GstPad* RtspServerBin::getBinSinkPad()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

RtspServerBin::RtspServerBin()
{
    // 생성자 코드 추가
}

// RecordBin 클래스의 소멸자 정의
RtspServerBin::~RtspServerBin()
{
    // 소멸자 코드 추가
}

gint RtspServerBin::init(guint8 num)
{
    GstPad *staticPad;
    ch = num;
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (%d)", _FILE_, __LINE__, __FUNCTION__, ch);

    re.bin = gst_bin_new(g_strdup_printf("rtspServerBin%d", ch));
    re.queue = gst_element_factory_make("queue", "queue");
    re.queue2 = gst_element_factory_make("queue", "queue2");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    re.parse = gst_element_factory_make("h264parse", "h264parse");
    re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.sink = gst_element_factory_make("appsink", "appsink");

    if (!re.bin || !re.queue || !re.capsfilter || !re.parse || !re.enc || !re.rate || !re.sink || !re.convert || !re.queue2)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] record element create error", _FILE_, __LINE__);
        return -1;
    }

    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.enc, re.parse, re.queue2, re.sink, re.capsfilter, NULL);

    if(!gst_bin_add(GST_BIN(pipeline), re.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, RTSP_FPS, 1, NULL);

    g_object_set(re.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    g_object_set(re.enc, "bitrate", RTSP_BITRATE, NULL);
    //g_object_set(re.rate, "max-rate", RTSP_FPS, "drop-only", FALSE, NULL);
    g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
    g_object_set(re.queue2, "max-size-time", GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(re.sink, "max-buffers", 60, NULL);
    g_object_set(re.sink, "drop", TRUE, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(re.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(re.sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(re.sink, "new-sample", G_CALLBACK(new_sample_handler), &rtspServerData);
    g_signal_connect(re.sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

    if (!gst_element_link_many(re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.parse, re.queue2, re.sink, NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
        return -1;
    }

    staticPad = gst_element_get_static_pad(re.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("rtspServerBin_sink_ch%d", ch), staticPad);

    if(!gst_element_add_pad(re.bin, sinkPad))
        g_error("error");

    gst_object_unref(staticPad);

    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    rtspServerData.ch = ch;
    rtspServerData.appSrcName = g_strdup_printf("%s%d", "appsrc", rtspServerData.ch);

    gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=10 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", rtspServerData.appSrcName);
    
    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    g_free(launch_str);
    
    //rtspServerData.appsrc = gst_element_factory_make("appsrc", rtspServerData.appSrcName);
    rtspServerData.appsrc = NULL;
    g_signal_connect(factory, "media-configure", (GCallback)media_configure, &rtspServerData);

    gchar *point = g_strdup_printf("/ch%d", rtspServerData.ch);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

    gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

    gst_rtsp_media_factory_add_role(factory, "semes",
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

    __LOG(LOG_NOTICE, "[RTSP][%s:%d]stream ready at rtsp://127.0.0.1:%s%s", _FILE_, __LINE__, RTSP_PORT, point);

    g_free(point);

    return 1;
}

#if 1
void rtspStop()
{
    g_object_unref(rtspServer);
}

gint rtspStart()
{
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    rtspServer = gst_rtsp_server_new ();
    g_object_set (rtspServer, "service", RTSP_PORT, NULL);

    rtspMounts = gst_rtsp_server_get_mount_points (rtspServer);

    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);

    g_signal_connect(rtspServer, "client-connected", G_CALLBACK(handle_client_connected), NULL);

#ifdef RTSP_AUTH_ENABLE
    /* make a new authentication manager */
    GstRTSPAuth *auth = gst_rtsp_auth_new ();

    /* make user token */
    GstRTSPToken *token =
        gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
        "semes", NULL);
    gchar *basic = gst_rtsp_auth_make_basic ("semes", "semes");
    gst_rtsp_auth_add_basic (auth, basic, token);
    g_free (basic);
    gst_rtsp_token_unref (token);
    gst_rtsp_server_set_auth (rtspServer, auth);
    g_object_unref (auth);
    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);
#endif

    if (gst_rtsp_server_attach (rtspServer, NULL) == 0) {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_, __LINE__);
        return -1;
    }

    return 1;
}
#endif