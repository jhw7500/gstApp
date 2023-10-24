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

RecordBin::RecordBin()
{
    // 생성자 코드 추가
}

// RecordBin 클래스의 소멸자 정의
RecordBin::~RecordBin()
{
    // 소멸자 코드 추가
}

gint RecordBin::init(guint8 num)
{
    //GstPad *ghostPad;
    GstPad *staticPad;
    ch = num;
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, ch);

    re.bin = gst_bin_new(g_strdup_printf("recordBin%d", ch));
    re.queue = gst_element_factory_make("queue", "queue");
    re.queue2 = gst_element_factory_make("queue", "queue2");
    re.parse = gst_element_factory_make("h264parse", "h264parse");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");

    if (!re.bin || !re.queue || !re.queue2 || !re.parse || !re.enc || !re.rate || !re.convert || !re.capsfilter)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] record element create error", _FILE_, __LINE__);
        return -1;
    }

    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.parse, re.queue2, NULL);

    if(!gst_bin_add(GST_BIN(pipeline), re.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, cmdArg.rec_fps, 1, NULL);

    g_object_set(re.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    //if(cmdArg.rec_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rec_fps, "drop-only", TRUE, NULL);

    g_object_set(re.enc, "bitrate", cmdArg.rec_bitrate, NULL);
    g_object_set(re.parse, "config-interval", -1, NULL);
    //g_object_set(re.rate, "max-rate", MAIN_FPS, "drop-only", FALSE, NULL);
    g_object_set(re.queue, "max-size-time", GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
    g_object_set(re.queue2, "max-size-time", GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);

    if (!gst_element_link_many(re.queue, re.rate, re.capsfilter, re.convert, re.enc, re.parse, re.queue2, NULL))
    //if (!gst_element_link_many(re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.parse, re.queue2, NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
        return -1;
    }

    staticPad = gst_element_get_static_pad(re.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("recordBin_sink_ch%d", ch), staticPad);

    if(!gst_element_add_pad(re.bin, sinkPad))
        g_error("error");

    gst_object_unref(staticPad);

    staticPad = gst_element_get_static_pad(re.queue2, "src");
    srcPad = gst_ghost_pad_new(g_strdup_printf("recordBin_src_ch%d", ch), staticPad);

    gst_object_unref(staticPad);

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

    return 0;
}
