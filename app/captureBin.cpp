/*
 *
 * Cantops captureBin.cpp support
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

#include "captureBin.h"

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static GstFlowReturn new_sample_handler(GstElement *sink, gpointer userData) 
{
    
    GstSample *sample;
    GstBuffer *buffer;
    CaptureData *info = (CaptureData *)userData;
    //guint8 mode;
    GstMapInfo map;
    FILE *file;
    gchar *path;

    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }
    buffer = gst_sample_get_buffer(sample);
    gst_sample_unref(sample);

    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        //gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        g_printerr("Failed to map buffer\n");
        return GST_FLOW_ERROR;
    }
    
    if(info->captureCnt >= cmdArg.captureMaxCnt)
    {
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_OK;
    }

    path = g_strdup_printf("%s_%d.jpg", info->filePath, info->captureCnt++);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] captureCnt %d, captureMax %d", _FILE_, __LINE__, info->captureCnt, cmdArg.captureMaxCnt);
    file = fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
    }

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

CaptureBin* CaptureBin::getInstance()
{
	static CaptureBin instance;
	return &instance;
}

CaptureBin::CaptureBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    sinkPad = NULL;
    be.bin = NULL;
    captureData.captureCnt = cmdArg.captureMaxCnt;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
}

gint CaptureBin::startCapture()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    captureData.captureCnt = 0;
    setFilePath();

    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    captureData.captureCnt = cmdArg.captureMaxCnt;

    return 1;
}

GstPad* CaptureBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gint CaptureBin::setFilePath()
{
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

    captureData.filePath = g_strdup_printf("%s/%s_%s-ch%d", cmdArg.captureDir, cmdArg.ohtName, date_str, ch);

    //__LOG(LOG_NOTICE, "[GST][%s:%d] filePath : %s", _FILE_, __LINE__, captureData.filePath);

    g_date_time_unref(datetime);
    g_free(date_str);

    return 1;
}

gint CaptureBin::init(guint8 num)
{
    gboolean ret = FALSE;
    GstPad *staticPad;
    ch = num;
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, ch);

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
    be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    be.parse = gst_element_factory_make("h264parse", "h264parse");
    be.enc = gst_element_factory_make("jpegenc", "jpegenc");
    be.rate = gst_element_factory_make("videorate", "videorate");
    be.sink = gst_element_factory_make("appsink", "appsink");
    be.crop = gst_element_factory_make("videocrop", "crop");

    if (!be.bin || !be.queue || !be.parse || !be.enc || !be.rate || !be.sink || !be.convert || !be.queue2 || !be.crop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.rate, be.convert, be.enc, be.parse, be.queue2, be.sink, be.crop, NULL);

    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    //if (!gst_element_link_many(be.queue, be.rate, be.convert, be.capsfilter, be.enc, be.parse, be.queue2, be.sink, NULL))
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture link err", _FILE_, __LINE__);
        return ret;
    }

    if (ch % 2 == 0)
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);
    else
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);

    //if(cmdArg.rtsp_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rtsp_fps, "drop-only", TRUE, NULL);

    //g_object_set(re.enc, "bitrate", cmdArg.rtsp_bitrate, NULL);
    g_object_set(be.queue, "max-size-time", GST_SECOND, "max-size-buffers", DEFAULT_MAIN_FPS, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND, "max-size-buffers", DEFAULT_MAIN_FPS, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(be.sink, "max-buffers", DEFAULT_MAIN_FPS, NULL);
    g_object_set(be.sink, "drop", TRUE, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(be.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(be.sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(be.sink, "new-sample", G_CALLBACK(new_sample_handler), &captureData);
    g_signal_connect(be.sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

    staticPad = gst_element_get_static_pad(be.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("captureBin_sink_ch%d", ch), staticPad);

    ret = gst_element_add_pad(be.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture pad add error in bin", _FILE_, __LINE__);
        return ret;
    }

    gst_object_unref(staticPad);

    return ret;
}