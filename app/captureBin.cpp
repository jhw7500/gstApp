/*
 *
 * Cantops captureBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "captureBin.h"
#include "tcpServer.h"

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
    gchar *path = NULL;

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

    if(info->captureCnt >= info->captureMaxCnt)
    {
        //info->mode = 0;
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_OK;
    }

    //g_print("captureCnt %d, captureMax %d\n", info->captureCnt, info->captureMaxCnt);
    
    if(info->mode == 0)
    {
        if(cmdArg.capture_encoder_en) path = g_strdup_printf("%s_%d.jpg", info->filePath, info->captureCnt);
        else path = g_strdup_printf("%s_%d.raw", info->filePath, info->captureCnt);
    }
    else if(info->mode == 1)
    {
        if(cmdArg.capture_encoder_en) path = g_strdup_printf("%s.jpg", info->filePath);
        else path = g_strdup_printf("%s.raw", info->filePath);
    }
    else if(info->mode == 2)
    {
        CTCPServer *tcpServer = CTCPServer::getInstance();
        tcpServer->sendDataTCP(tcpServer->m_clientSocket, (gchar *)map.data, map.size);
        //tcpServer->frameData.data = map.data;
        //tcpServer->frameData.size = map.size;
        //tcpServer->cap_step = 2;

        //gchar *date_str = g_date_time_format(g_date_time_new_now_local(), "%Y%m%d_%H%M%S");
        //info->filePath = g_strdup_printf("%s/%s_%s-ch%d", cmdArg.captureDir, cmdArg.ohtName, date_str, info->ch);
        //g_free(date_str);

        if(cmdArg.capture_encoder_en) path = g_strdup_printf("%s.jpg", info->filePath);
        else path = g_strdup_printf("%s.raw", info->filePath);
    }

    info->captureCnt++;

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

GstState CaptureBin::getState()
{
    GstState state;
    gst_element_get_state(be.bin, &state, NULL, GST_CLOCK_TIME_NONE);

    return state;
}

GstStateChangeReturn CaptureBin::setState(GstState state)
{
    //gst_element_set_state(be.sink, state);
    //gst_element_set_state(be.convert, state);
    //gst_element_set_state(be.crop, state);
    //gst_element_set_state(be.queue, state);
    
    return gst_element_set_state(be.bin, state);
    //return gst_element_change_state(be.bin, GST_STATE_CHANGE_PLAYING_TO_PAUSED);
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
    captureData.mode = 0;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, captureData.ch);
}

gint CaptureBin::startCapture(guint8 mode)
{
    if(mode == 0) captureData.captureMaxCnt = cmdArg.captureMaxCnt;
    else if(mode == 1) captureData.captureMaxCnt = cmdArg.main_fps*60;
    else if(mode == 2) captureData.captureMaxCnt = 1;

    setFilePath();
    captureData.mode = mode;
    captureData.captureCnt = 0;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s mode : %d, maxCnt:%d", _FILE_, __LINE__, __FUNCTION__, mode, captureData.captureMaxCnt);

    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    captureData.captureCnt = cmdArg.captureMaxCnt;
    captureData.mode = 0;

    return 1;
}

gboolean CaptureBin::addBinToPipe(GstElement *pipe)
{
    gboolean ret = gst_bin_add(GST_BIN(pipe), be.bin);
    
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin add error in pipeline", _FILE_, __LINE__);
    }
    return ret;
}

gboolean CaptureBin::removeBinToPipe(GstElement *pipe)
{
    
    gboolean ret = gst_bin_remove(GST_BIN(pipe), be.bin);
    
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin remove error in pipeline", _FILE_, __LINE__);
    }
    return ret;
}

GstPad* CaptureBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, captureData.ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gint CaptureBin::setFilePath()
{
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

    captureData.filePath = g_strdup_printf("%s/%s_%s-ch%d", cmdArg.captureDir, cmdArg.ohtName, date_str, captureData.ch);

    __LOG(LOG_NOTICE, "[GST][%s:%d] filePath : %s", _FILE_, __LINE__, captureData.filePath);

    g_date_time_unref(datetime);
    g_free(date_str);

    return 1;
}

gint CaptureBin::init(guint8 num, gboolean crop_en)
{
    gint ret = 0;
    GstPad *staticPad;
    captureData.ch = num;
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d, crop : %s", _FILE_, __LINE__, __FUNCTION__, captureData.ch, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", captureData.ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
    be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    //be.parse = gst_element_factory_make("h264parse", "h264parse");
    be.enc = gst_element_factory_make("jpegenc", "jpegenc");
    //be.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    be.rate = gst_element_factory_make("videorate", "videorate");
    be.sink = gst_element_factory_make("appsink", "appsink");
    be.crop = gst_element_factory_make("videocrop", "crop");
    be.overlay = gst_element_factory_make("textoverlay", "overlay");

    if (!be.bin || !be.queue || !be.enc || !be.rate || !be.sink || !be.convert || !be.queue2 || !be.crop || !be.overlay ) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.rate, be.convert, be.enc, be.queue2, be.sink, be.crop, be.overlay, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), be.queue, be.sink, NULL);

#if 0
    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }
#endif

#ifdef CHANNEL_EACH_CROP
    if(cmdArg.capture_encoder_en)
    {
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.convert, be.enc, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.convert, be.enc, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
        else ret = gst_element_link_many(be.queue, be.enc, be.queue2, be.sink, NULL);
    }
    else
    {
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.convert, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.convert, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.convert, be.sink, NULL);
        else {
            g_print("be.queue, be.sink\n");
            ret = gst_element_link_many(be.queue, be.sink, NULL);
            //ret = gst_element_link_many(be.queue, be.rate, be.capsfilter, be.enc, be.parse, be.queue2, be.sink, NULL);
        }
    }
#else
    ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    //if (!gst_element_link_many(be.queue, be.rate, be.convert, be.capsfilter, be.enc, be.parse, be.queue2, be.sink, NULL))
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture link err", _FILE_, __LINE__);
        return ret;
    }

    if (captureData.ch % 2 == 0)
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
    else
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);

    //if(cmdArg.rtsp_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rtsp_fps, "drop-only", TRUE, NULL);

    //g_object_set(re.enc, "bitrate", cmdArg.rtsp_bitrate, NULL);
    g_object_set(be.queue, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.main_fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.main_fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(be.sink, "max-buffers", cmdArg.main_fps, NULL);
    g_object_set(be.sink, "drop", TRUE, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(be.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(be.sink, "async", TRUE, NULL);
    g_signal_connect(be.sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(be.sink, "new-sample", G_CALLBACK(new_sample_handler), &captureData);
    g_signal_connect(be.sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

    staticPad = gst_element_get_static_pad(be.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("captureBin_sink_ch%d", captureData.ch), staticPad);

    ret = gst_element_add_pad(be.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture pad add error in bin", _FILE_, __LINE__);
        return ret;
    }

    gst_object_unref(staticPad);

    return ret;
}