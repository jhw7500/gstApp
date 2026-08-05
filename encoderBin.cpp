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
    re.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
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
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.rate, re.enc, re.tee, NULL);
        else
            ret = gst_element_link_many(re.queue, re.crop, re.convert, re.enc, re.tee, NULL);
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
     * Defaults reproduce the previous behaviour exactly: gop-size stays at
     * DEFAULT_GOP_SIZE (15), the rest match the plugin's own defaults. */
    g_object_set(re.enc, "bitrate", cmdArg.cam[ch].bps[STREAM_REC], NULL);
    g_object_set(re.enc, "gop-size", cmdArg.cam[ch].gop[STREAM_REC], NULL);
    /* quant is installed per codec (the VPU_V_AVC branch in gstvpuenc.c), not
     * per SoC, so it exists wherever vpuenc_h264 does — no guard needed. */
    g_object_set(re.enc, "quant", cmdArg.cam[ch].quant[STREAM_REC], NULL);
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
