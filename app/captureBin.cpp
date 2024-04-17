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
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) 
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    return GST_FLOW_OK;
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
    gchar *extention = NULL;

    //g_print("%s\n", __FUNCTION__);
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

#if 0
    info->buf = gst_buffer_new_and_alloc(map.size);
    gst_buffer_fill(info->buf, 0, map.data, map.size);
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
#endif

    if(info->captureCnt >= info->captureMaxCnt)
    {
        //info->mode = 0;
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_OK;
    }

    //g_print("captureCnt %d, captureMax %d\n", info->captureCnt, info->captureMaxCnt);
    if(cmdArg.capture_encoder_en) extention = g_strdup_printf("%s", "jpg");
    else extention = g_strdup_printf("%s", "rgb");

    if(info->captureMaxCnt > 1 && info->captureMaxCnt < cmdArg.fps[STREAM_CAP][info->ch]) 
    {
        path = g_strdup_printf("%s_%d.%s", info->filePath, info->captureCnt, extention);
        __LOG(LOG_NOTICE, "[GST][%s:%d] path : %s, cnt : %d, max : %d", _FILE_, __LINE__, info->filePath, info->captureCnt, info->captureMaxCnt);

    }
    else path = g_strdup_printf("%s.%s", info->filePath, extention);

    
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
    if(path != NULL) g_free(path);
    if(extention != NULL) g_free(extention);

    return GST_FLOW_OK;
}

void CaptureBin::setAppsrc(GstElement *appsrc)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, captureData.ch);

    if(appsrc == NULL) __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d appsrc is NULL!", _FILE_, __LINE__, __FUNCTION__, captureData.ch);

    captureData.appsrc = appsrc;

    return;
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
    captureData.captureMaxCnt = cmdArg.captureMaxCnt;
    captureData.mode = 0;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, captureData.ch);
}

gint CaptureBin::startCapture(gint maxCnt)
{
    //if (getBinSinkPad() == NULL) return 0;
    //setFilePath();
    //captureData.mode = mode;

    captureData.captureMaxCnt = maxCnt;

    captureData.captureCnt = 0;
    __LOG(LOG_NOTICE, "[GST][%s:%d] cnt:%d, maxCnt:%d", _FILE_, __LINE__, captureData.captureCnt, captureData.captureMaxCnt);

    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    captureData.captureCnt = cmdArg.captureMaxCnt;
    captureData.mode = 0;

    return 1;
}

gint CaptureBin::getCaptureCnt()
{
    return captureData.captureCnt;
}

gboolean CaptureBin::addBinToPipe(GstElement *pipe)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, captureData.ch, __FUNCTION__);

    if(gst_bin_get_by_name(GST_BIN(pipe), g_strdup_printf("captureBin%d", captureData.ch)) != NULL)
    {
        __LOG(LOG_INFO, "[GST][%s:%d] ch%d capture bin is already added", _FILE_, __LINE__, captureData.ch);
        return 1;
    }

    return gst_bin_add(GST_BIN(pipe), be.bin);
}

gboolean CaptureBin::removeBinToPipe(GstElement *pipe)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, captureData.ch, __FUNCTION__);

    return gst_bin_remove(GST_BIN(pipe), be.bin);
}

GstPad* CaptureBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, captureData.ch, __FUNCTION__);
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

gboolean CaptureBin::init(guint8 num, gboolean crop_en)
{
    gboolean ret = 0;
    GstPad *staticPad;
    GstCaps *caps;
    captureData.ch = num;
    captureData.fps = cmdArg.main_fps[num/2];
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d, crop : %s", _FILE_, __LINE__, __FUNCTION__, captureData.ch, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", captureData.ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", captureData.ch));
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d_2", captureData.ch));
    be.imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imx_convert%d", captureData.ch));
    be.convert = gst_element_factory_make("videoconvert", g_strdup_printf("convert%d", captureData.ch));
    //be.parse = gst_element_factory_make("h264parse", "h264parse");
    be.enc = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", captureData.ch));
    //be.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    be.rate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", captureData.ch));
    be.sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", captureData.ch));
    be.crop = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", captureData.ch));
    be.overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", captureData.ch));
    be.capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", captureData.ch));

    if (!be.bin || !be.queue || !be.enc || !be.rate || !be.sink || !be.imx_convert || !be.queue2 || !be.crop || !be.overlay || !be.capsfilter || !be.convert) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.rate, be.imx_convert, be.enc, be.queue2, be.sink, be.crop, be.overlay, be.capsfilter, be.convert, NULL);
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
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.imx_convert, be.rate, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.imx_convert, be.rate, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.imx_convert, be.rate, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else ret = gst_element_link_many(be.queue, be.rate, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
    }
    else
    {
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.imx_convert, be.rate, be.capsfilter, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.imx_convert, be.rate, be.capsfilter, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.imx_convert, be.rate, be.capsfilter, be.queue2, be.sink, NULL);
        else ret = gst_element_link_many(be.queue, be.rate, be.capsfilter, be.queue2, be.sink, NULL);
    }
#else
    ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    //if (!gst_element_link_many(be.queue, be.rate, be.convert, be.capsfilter, be.enc, be.parse, be.queue2, be.sink, NULL))
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture link err", _FILE_, __LINE__);
        return ret;
    }

    caps = gst_caps_new_simple("video/x-raw",
                                //"format", G_TYPE_STRING, "RGB16",
                                "width", G_TYPE_INT, cmdArg.width,
                                "height", G_TYPE_INT, cmdArg.height,
                                "framerate", GST_TYPE_FRACTION, cmdArg.fps[STREAM_CAP][captureData.ch], 1,
                                NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (captureData.ch % 2 == 0)
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
    else
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);

    //if(cmdArg.rtsp_fps >= 25) g_object_set(re.rate, "max-rate", cmdArg.rtsp_fps, "drop-only", TRUE, NULL);

    //g_object_set(re.enc, "bitrate", cmdArg.rtsp_bitrate, NULL);
    g_object_set(be.queue, "max-size-time", GST_SECOND, "max-size-buffers", captureData.fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND, "max-size-buffers", captureData.fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(be.sink, "max-buffers", captureData.fps, NULL);
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