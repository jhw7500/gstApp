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

#include "recordBin.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#if 0
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
    RecordData *info = (RecordData *)userData;
    GstMapInfo map;

    g_print("%s\n", __FUNCTION__);
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
    //gst_sample_unref(sample);
    if(info->debug)
    {
        GstClockTime timestamp = GST_BUFFER_PTS(buffer);
        g_message("Timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(timestamp));
        //info->debug = 0;
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
    if(info->appsrc == NULL || GST_STATE(GST_ELEMENT(info->appsrc)) != GST_STATE_PLAYING)
    {
        //g_print("appsrc null return!\n");
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
#endif

    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
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
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}
#endif

RecordBin* RecordBin::getInstance()
{
	static RecordBin instance;
	return &instance;
}

GstElement* RecordBin::getBinAppsrc()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    return gst_bin_get_by_name_recurse_up(GST_BIN(re.bin), recordData.appSrcName);
    //return recordData.appsrc;
}

GstPad* RecordBin::getBinSrcPad()
{
    if(srcPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_src_ch%d", ch));
    return srcPad;
}

GstPad* RecordBin::getBinSinkPad()
{
    if(sinkPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

void RecordBin::setDualBps(gboolean val)
{
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d val:%d", _FILE_, __LINE__, __FUNCTION__, ch, val);
    recordData.dual_bps = val;
}


void RecordBin::setOverlayText(gchar *text)
{
    if (re.overlay == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d overlay not initialized", _FILE_, __LINE__, ch);
        return;
    }
    g_object_set(re.overlay, "text", text, NULL);
}

void RecordBin::getBitrate()
{
    gint bps;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
     g_print("rec ch%d get bitrate : %d\n", ch, bps);
}

void RecordBin::setBitrate(gint data)
{
    gint bps;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    g_object_set(re.enc, "bitrate", data, NULL);
    g_object_get(re.enc, "bitrate", &bps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_print("rec ch%d set bitrate : %d\n", ch, bps);
}

void RecordBin::getFps()
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

    if (re.capsfilter == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d capsfilter not initialized", _FILE_, __LINE__, ch);
        return;
    }

    g_object_get(re.capsfilter, "caps", &caps, NULL);
    caps_str = gst_caps_to_string(caps);

    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get caps : %s", _FILE_, __LINE__, ch, caps_str);
    g_print("rec ch%d get caps : %s\n", ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);
}

void RecordBin::setFps(guint16 data)
{
    //gint fps;
    GstCaps *caps;
    gchar *caps_str;

    if (re.capsfilter == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d capsfilter not initialized", _FILE_, __LINE__, ch);
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
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set caps : %s", _FILE_, __LINE__, ch, caps_str);
    g_print("rec ch%d set caps : %s\n", ch, caps_str);

    gst_caps_unref(caps);
    g_free(caps_str);
}

void RecordBin::setRotation(guint16 data)
{
    gint rotation;

    if (re.convert == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d converter not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.convert, "rotation", data, NULL);
    g_object_get(re.convert, "rotation", &rotation, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set rotation : %d", _FILE_, __LINE__, ch, rotation);
    g_print("rec ch%d set rotation : %d\n", ch, rotation);
}

void RecordBin::getRotation()
{
    gint rotation;

    if (re.convert == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d converter not initialized", _FILE_, __LINE__, ch);
        return;
    }

    g_object_get(re.convert, "rotation", &rotation, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get rotation : %d", _FILE_, __LINE__, ch, rotation);
    g_print("rec ch%d get rotation : %d\n", ch, rotation);
}

void RecordBin::forceKeyframe()
{
    if (re.enc == NULL)
        return;

    GstEvent *event = gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0);
    if (!gst_element_send_event(re.enc, event))
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d force keyframe send failed", _FILE_, __LINE__, ch);
}

void RecordBin::setGop(guint16 data)
{
    gint gop;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "gop-size", data, NULL);
    g_object_get(re.enc, "gop-size", &gop, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set gop_size : %d", _FILE_, __LINE__, ch, gop);
    g_print("rec ch%d set gop_size : %d\n", ch, gop);
}

void RecordBin::getGop()
{
    gint gop;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    g_object_get(re.enc, "gop-size", &gop, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get gop_size : %d", _FILE_, __LINE__, ch, gop);
    g_print("rec ch%d get gop_size : %d\n", ch, gop);
}

void RecordBin::getKeyframe()
{
    gint key;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get keyframe : %d", _FILE_, __LINE__, ch, key);
    g_print("rec ch%d get keyframe : %d\n", ch, key);
}

void RecordBin::setkeyframe(guint16 data)
{
    gint key;

    if (re.enc == NULL) {
        __LOG(LOG_ERR, "[GST][%s:%d] ch%d encoder not initialized", _FILE_, __LINE__, ch);
        return;
    }

    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);
    g_object_set(re.enc, "set-keyframe", data, NULL);
    g_object_get(re.enc, "set-keyframe", &key, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d set keyframe : %d", _FILE_, __LINE__, ch, key);
    g_print("rec ch%d set keyframe : %d\n", ch, key);
}

GstState RecordBin::getState()
{
    GstState state;

    gst_element_get_state(re.bin, &state, NULL, GST_CLOCK_TIME_NONE);
 
    return state;
}

GstStateChangeReturn RecordBin::setState(GstState state)
{
    return gst_element_set_state(re.bin , state);
}

gboolean RecordBin::addBinToPipe(GstElement *pipe)
{
    if(gst_bin_get_by_name(GST_BIN(pipe), g_strdup_printf("captureBin%d", recordData.ch)) != NULL)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d capture bin is already added", _FILE_, __LINE__, recordData.ch);
        return 1;
    }

    return gst_bin_add(GST_BIN(pipe), re.bin);
}

gboolean RecordBin::removeBinToPipe(GstElement *pipe)
{
    //gst_element_set_state(be.bin, GST_STATE_NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d %s", _FILE_, __LINE__, recordData.ch, __FUNCTION__);

    return gst_bin_remove(GST_BIN(pipe), re.bin);
}

gboolean RecordBin::addBinRtspSrcPad(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    GstPad *target_pad = gst_element_get_request_pad(re.tee, "src_%u");
    srcRtspPad = gst_ghost_pad_new(g_strdup_printf("rtsp_pad_ch%d", ch), target_pad);
    if (target_pad)
        gst_object_unref(target_pad);

    return gst_element_add_pad(re.bin, srcRtspPad);
}

GstPad* RecordBin::getBinRtspSrcPad(guint8 ch)
{
    if(srcRtspPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    return srcRtspPad;
}

RecordBin::RecordBin()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    sinkPad = NULL;
    srcPad = NULL;
    re.bin = NULL;
    recordData.appsrc = NULL;
    recordData.debug = FALSE;
    recordData.dual_bps = TRUE;
}

// RecordBin 클래스의 소멸자 정의
RecordBin::~RecordBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
}

gboolean RecordBin::init(guint8 num, gboolean crop_en)
{
    gboolean ret = 0;
    ch = num;
    recordData.ch = ch;
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d, crop %s", _FILE_, __LINE__, __FUNCTION__, ch, crop_en? "enable":"disable");

    re.bin = gst_bin_new(g_strdup_printf("recordBin%d", ch));
    re.bin2 = gst_bin_new(g_strdup_printf("recordBin%d_2", ch));
    re.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    re.queue2 = gst_element_factory_make(QUEUE_TYPE, "queue2");
    re.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    const gchar *encoder_factory =
        g_strcmp0(cmdArg.enc, ENC_H265) == 0 ? "vpuenc_hevc" : "vpuenc_h264";
    re.enc = gst_element_factory_make(encoder_factory, encoder_factory);
    re.rate = gst_element_factory_make("videorate", "videorate");
    re.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    re.crop = gst_element_factory_make("videocrop", "crop");
    //re.overlay = gst_element_factory_make("textoverlay", "overlay");
    re.overlay = gst_element_factory_make("timeoverlay", "overlay");
    re.tee = gst_element_factory_make("tee", "tee");
    re.appsink = gst_element_factory_make("appsink", "appsink");
    recordData.appSrcName = g_strdup_printf("record_appsrc%d", ch);
    re.appsrc = gst_element_factory_make("appsrc", recordData.appSrcName);
    recordData.appsrc = re.appsrc;

    if (!re.bin || !re.queue || !re.queue2 || !re.enc || !re.rate || !re.convert || !re.capsfilter || !re.crop || !re.overlay || !re.appsink || !re.appsrc || !re.tee) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record element create error", _FILE_, __LINE__);
        return ret;
    }

#if 0
    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.appsink, NULL);
    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    ret = gst_element_link_many(re.queue, re.crop, re.convert, re.capsfilter, re.appsink, NULL);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }
#if 0
    gst_bin_add_many(GST_BIN(re.bin2), re.appsrc, re.enc, re.parse, re.queue2, NULL);
    ret = gst_bin_add(GST_BIN(pipeline), re.bin2);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }

    ret = gst_element_link_many(re.appsrc, re.enc, re.parse, re.queue2, NULL);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }
#endif

#endif

#if 1
    //gst_bin_add_many(GST_BIN(re.bin), re.appsrc, re.sink, NULL);
    gst_bin_add_many(GST_BIN(re.bin), re.queue, re.rate, re.convert, re.capsfilter, re.enc, re.queue2, re.crop, re.overlay, re.tee, NULL);
    ret = gst_bin_add(GST_BIN(pipeline), re.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
        return ret;
    }

#ifdef CHANNEL_EACH_CROP
    if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.crop, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, NULL);
    else if(cmdArg.overlay_en) ret = gst_element_link_many(re.queue, re.overlay, re.convert, re.rate, re.capsfilter, re.enc, NULL);
    else if(crop_en) {
        //ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.tee, NULL);
        ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, NULL);
#if 0
        if(recordData.dual_bps == TRUE)
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
        else
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.tee, NULL);
#endif
    }
    //else if(crop_en) ret = gst_element_link_many(re.queue, re.convert, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
    else ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, NULL);
    //if(cmdArg.mode) ret = gst_element_link_many(re.queue, re.crop, re.convert, re.enc, re.parse, re.queue2, NULL);
#else
    ret = gst_element_link_many(re.queue, re.rate, re.capsfilter, re.enc, re.parse, re.queue2, NULL);
#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record link err", _FILE_, __LINE__);
        return ret;
    }
#endif

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        //"format", G_TYPE_STRING, "NV12",
                                        //"width", G_TYPE_INT, cmdArg.width,
                                        //"height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.fps[STREAM_REC][ch], 1,
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

    g_object_set(re.enc, "bitrate", cmdArg.cam[ch].bps[STREAM_REC], NULL);
    g_object_set(re.enc, "gop-size", cmdArg.cam[ch].gop[STREAM_REC], NULL);
    /* quant is codec-specific; skip it cleanly when the selected encoder does
     * not expose the property. */
    enc_set_optional_int(re.enc, "quant", cmdArg.cam[ch].quant[STREAM_REC], ch);
    enc_set_optional_int(re.enc, "profile", cmdArg.cam[ch].profile[STREAM_REC], ch);
    enc_set_optional_int(re.enc, "qp-min", cmdArg.cam[ch].qp_min[STREAM_REC], ch);
    enc_set_optional_int(re.enc, "qp-max", cmdArg.cam[ch].qp_max[STREAM_REC], ch);
    //g_object_set(re.parse, "config-interval", -1, NULL);
    //g_object_set(re.rate, "max-rate", MAIN_FPS, "drop-only", FALSE, NULL);
    // [Queue 최적화] I/O 지연 대비 시간 기반 버퍼링 (JSON 설정값 사용)
    g_object_set(re.queue, "max-size-time", cmdArg.queue_rec_sink_time_ms*GST_MSECOND, "max-size-buffers", 0, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(re.queue2, "max-size-time", cmdArg.queue_rec_sink_time_ms*GST_MSECOND, "max-size-buffers", 0, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.convert, "videocrop-meta-enable", TRUE, NULL);
    //g_object_set(re.enc, "bitrate", cmdArg.rtsp_bitrate, NULL);

#if 0
    g_object_set(re.appsrc, "do-timestamp", 1, NULL);
    g_object_set(re.appsrc, "name", recordData.appSrcName, NULL);
    g_object_set(re.appsrc, "min-percent", 0, NULL);
    g_object_set(re.appsrc, "is-live", 1, NULL);
    g_object_set(re.appsrc, "format", 3, NULL);
    g_object_set(re.appsrc, "block", TRUE, NULL);
    g_object_set(re.appsink, "max-buffers", cmdArg.fps[STREAM_REC][ch], NULL);
    g_object_set(re.appsink, "drop", TRUE, NULL);
    g_object_set(re.appsink, "emit-signals", TRUE, "sync", FALSE, NULL);
    recordData.appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(re.bin2), recordData.appSrcName);
    g_signal_connect(re.appsink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(re.appsink, "new-sample", G_CALLBACK(new_sample_handler), &recordData);
    g_signal_connect(re.appsink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );
#endif

    if(cmdArg.levelMode == MODE_TEST)
    {
        gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.enc, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.enc, NULL);
        gst_pad_add_probe(gst_element_get_static_pad(re.queue, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.queue, NULL);
        gst_pad_add_probe(gst_element_get_static_pad(re.queue2, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.queue2, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.rate, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.rate, NULL);
        //gst_pad_add_probe(gst_element_get_static_pad(re.convert, "src"), GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, re.convert, NULL);
    }

#if 1
    sinkPad = gst_ghost_pad_new(g_strdup_printf("sinkpad_ch%d", ch), gst_element_get_static_pad(re.queue, "sink"));
    ret = gst_element_add_pad(re.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
        return ret;
    }
#endif

#if 1

#if 1
    //if(gst_pad_link(gst_element_get_request_pad(re.tee, "src_record%u"), gst_element_get_static_pad(re.queue2, "sink")) != GST_PAD_LINK_OK)    //if(!gst_element_link(re.parse, re.sink))
    //{
    //    __LOG(LOG_CRIT, "[GST][%s:%d] record link error in pipeline", _FILE_, __LINE__);
    //    return -1;
    //}
#endif
    srcPad = gst_ghost_pad_new(g_strdup_printf("srcpad_ch%d", ch), gst_element_get_static_pad(re.enc, "src"));
    ret = gst_element_add_pad(re.bin, srcPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
        return ret;
    }
    //srcPad = gst_ghost_pad_new(g_strdup_printf("record_pad_ch%d", ch), gst_element_get_request_pad(re.tee, "src_%u"));
    //ret = gst_element_add_pad(re.bin, srcPad);
    //if(!ret) {
    //    __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
    //    return ret;
    //}
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
