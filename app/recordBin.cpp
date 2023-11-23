/*
 *
 * Cantops recordBin.cpp support
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

#include "recordBin.h"

RecordBin* RecordBin::getInstance()
{
	static RecordBin instance;
	return &instance;
}

GstPad* RecordBin::getBinSrcPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_src_ch%d", ch));
    return srcPad;
}

GstPad* RecordBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gboolean RecordBin::setOverlayText(gchar *text)
{
    g_object_set(re.overlay, "text", text, NULL);

    return 1;
}

gboolean RecordBin::getBitrate()
{
    gint bps;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);

    return 1;
}

gboolean RecordBin::setBitrate(guint16 data)
{
    gint bps;

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "bitrate", data, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set bitrate : %d", _FILE_, __LINE__, ch, bps);

    return 1;
}

gboolean RecordBin::getFps()
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

    g_object_get(re.capsfilter, "caps", &caps, NULL);
    caps_str = gst_caps_to_string(caps);

    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);

    return 1;
}

gboolean RecordBin::setFps(guint16 data)
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

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
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set caps : %s", _FILE_, __LINE__, ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);

    return 1;
}

RecordBin::RecordBin()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    sinkPad = NULL;
    srcPad = NULL;
    re.bin = NULL;
}

// RecordBin 클래스의 소멸자 정의
RecordBin::~RecordBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

gint RecordBin::init(guint8 num)
{
    gboolean ret = FALSE;
    ch = num;
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, ch);

    re.bin = gst_bin_new(g_strdup_printf("recordBin%d", ch));
    re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    re.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
    re.parse = gst_element_factory_make("h264parse", "h264parse");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    re.convert2 = gst_element_factory_make("imxvideoconvert_g2d", "convert2");
    re.crop = gst_element_factory_make("videocrop", "crop");
    re.overlay = gst_element_factory_make("textoverlay", "overlay");

    if (!re.bin || !re.queue || !re.queue2 || !re.parse || !re.enc || !re.rate || !re.convert || !re.capsfilter || !re.crop || !re.overlay || !re.convert2) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.parse, re.queue2, re.crop, re.overlay, re.convert2, NULL);
    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

#ifdef CHANNEL_EACH_CROP
    if(cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
    else ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
    //if(cmdArg.mode) ret = gst_element_link_many(re.queue, re.crop, re.convert, re.enc, re.parse, re.queue2, NULL);
#else
    ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.convert, re.enc, re.parse, re.queue2, NULL);
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, cmdArg.rec_fps, 1, NULL);

    g_object_set(re.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (ch % 2 == 0)
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);
    else
        g_object_set(re.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
        
    //if(cmdArg.rec_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rec_fps, "drop-only", TRUE, NULL);
    g_object_set(re.overlay, "valignment", 2, NULL);
    g_object_set(re.overlay, "halignment", 0, NULL);
    g_object_set(re.overlay, "font-desc", DEFAULT_OVERLAY_FONT, NULL);

    g_object_set(re.enc, "bitrate", cmdArg.rec_bitrate, NULL);
    g_object_set(re.enc, "gop-size", 15, NULL);
    //g_object_set(re.parse, "config-interval", -1, NULL);
    //g_object_set(re.rate, "max-rate", MAIN_FPS, "drop-only", FALSE, NULL);
    g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.rec_fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(re.queue2, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.rec_fps, "leaky", LEAKY_DOWNSTREAM, NULL);

    sinkPad = gst_ghost_pad_new(g_strdup_printf("recordBin_sink_ch%d", ch), gst_element_get_static_pad(re.queue, "sink"));

    if(cmdArg.testMode == TEST_MODE)
    {
        gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        gst_pad_add_probe(gst_element_get_static_pad(re.queue, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.queue, NULL);
        gst_pad_add_probe(gst_element_get_static_pad(re.queue2, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.queue2, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.rate, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.rate, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.convert, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.convert, NULL);
    }

    if(!gst_element_add_pad(re.bin, sinkPad))
        g_error("error");

    srcPad = gst_ghost_pad_new(g_strdup_printf("recordBin_src_ch%d", ch), gst_element_get_static_pad(re.queue2, "src"));

#if 1
    if(!gst_element_add_pad(re.bin, srcPad))
        g_error("error");
#else

    re.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    g_object_set(re.sink, "max-size-time", 60*GST_SECOND, NULL);
    if(ch == 0) g_object_set(re.sink, "location", "test_ch0_%02d.mp4", NULL);
    else if(ch == 1) g_object_set(re.sink, "location", "test_ch1_%02d.mp4", NULL);
    else if(ch == 2) g_object_set(re.sink, "location", "test_ch2_%02d.mp4", NULL);
    else if(ch == 3) g_object_set(re.sink, "location", "test_ch3_%02d.mp4", NULL);

    if(!gst_bin_add(GST_BIN(re.bin), re.sink))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    if(gst_pad_link(gst_element_get_static_pad(re.parse, "src"), gst_element_get_request_pad(re.sink, "video_aux_%u")) != GST_PAD_LINK_OK)    //if(!gst_element_link(re.parse, re.sink))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink link error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    //sinkPad = gst_element_get_request_pad(sink, "audio_%u");

#endif

    return ret;
}
