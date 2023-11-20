/*
 *
 * Cantops videoBin.cpp support
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

#include "videoBin.h"

VideoBin* VideoBin::getInstance()
{
	static VideoBin instance;
	return &instance;
}

GstPad* VideoBin::getBinRtspSrcPad(ChannelNum ch)
{
    return srcRtspPad;
}

GstPad* VideoBin::getBinRecordSrcPad(ChannelNum ch)
{
    return srcRecordPad;
}

GstPad* VideoBin::getBinCaptureSrcPad(ChannelNum ch)
{
    return srcCapturePad;
}

gint VideoBin::addBinRtspSrcPad(ChannelNum ch)
{
    if(be.crop[ch%2] == NULL)
        srcRtspPad = gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));
    else
        srcRtspPad = gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch%2), gst_element_get_request_pad(be.tee[ch%2], "src_%u"));

    if (!gst_element_add_pad(be.bin, srcRtspPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    return 0;
}

gint VideoBin::addBinRecordSrcPad(ChannelNum ch)
{
    if(be.crop[ch%2] == NULL)
        srcRecordPad = gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));
    else
        srcRecordPad = gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch%2), gst_element_get_request_pad(be.tee[ch%2], "src_%u"));

    if (!gst_element_add_pad(be.bin, srcRecordPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    if(cmdArg.mode == TEST_MODE)
    {
        gst_pad_add_probe(srcRecordPad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.bin, NULL);
    }

    return 0;
}

gint VideoBin::addBinCaptureSrcPad(ChannelNum ch)
{
    if(be.crop[ch%2] == NULL)
        srcCapturePad = gst_ghost_pad_new(g_strdup_printf("videoBin_capture_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));
    else
        srcCapturePad = gst_ghost_pad_new(g_strdup_printf("videoBin_capture_src_ch%d", ch%2), gst_element_get_request_pad(be.tee[ch%2], "src_%u"));

    if (!gst_element_add_pad(be.bin, srcCapturePad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    return 0;
}

VideoBin::VideoBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    be.bin = NULL;
    be.crop[0] = NULL;
    be.crop[1] = NULL;
    srcRecordPad = NULL;
    srcRtspPad = NULL;
}

VideoBin::~VideoBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

gint VideoBin::addCrop(CropDir dir)
{
    //g_print("csi:%d, dir:%d\n", csi, dir);
    
    be.crop[dir] = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", dir));
    be.tee[dir] = gst_element_factory_make("tee", g_strdup_printf("tee%d", dir));
    be.overlay[dir] = gst_element_factory_make("timeoverlay", g_strdup_printf("overaly%d", dir));
    be.queue[dir] = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", dir));

    if (dir % 2 == 0)
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
    else
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);

    if (!be.crop[dir] || !be.tee[dir] || !be.overlay[dir] || !be.queue[dir])
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_, __LINE__, dir);
        return -1;
    }

    g_object_set(be.overlay[dir], "valignment", 2, NULL);
    g_object_set(be.overlay[dir], "halignment", 0, NULL);
    g_object_set(be.overlay[dir], "font-desc", "Times New Roman Italic, 16", NULL);
    g_object_set(be.overlay[dir], "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(be.overlay[dir], "show-times-as-dates", TRUE, NULL);
    g_object_set(be.overlay[dir], "datetime-epoch", g_date_time_new_now_local(), NULL);
    g_object_set(be.queue[dir], "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.main_fps, "leaky", 1, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, cmdArg.res[cmdArg.resMode].width*2,
                                        "height", G_TYPE_INT, cmdArg.res[cmdArg.resMode].height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps, 1,
                                        NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(be.bin), be.crop[dir], be.tee[dir], be.overlay[dir], be.queue[dir], NULL);
    if (!gst_element_link_many(be.teeCrop, be.queue[dir], be.crop[dir], be.tee[dir], NULL))
    //if (!gst_element_link_many(ve.teeCrop, ve.crop[dir], ve.overlay[dir], ve.tee[dir], NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] crop [%d] link err", _FILE_, __LINE__, dir);
        return -1;
    }

#if 0
    guint8 ch = csi * 2 + dir;
    g_print("ch:%d\n", ch);
    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error0");
    else
        g_message("videoBin_record_src_ch%d", ch);

    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error1");
    else
        g_message("videoBin_rtsp_src_ch%d", ch);
#endif

    return 0;
}

gint VideoBin::init(CsiNum num)
{
    csi = num;

    if(be.bin != NULL)
    {
        //__LOG(LOG_NOTICE, "[GST][%s:%d] %s(%d) already init", _FILE_, __LINE__, __FUNCTION__, csi);
        return 0;
    }

    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, csi);

    be.bin = gst_bin_new(g_strdup_printf("videoBin%d", csi));
    be.src = gst_element_factory_make("v4l2src", "src");
    be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    be.capsfilter = gst_element_factory_make("capsfilter", "caps");
    be.teeCrop = gst_element_factory_make("tee", "teeCrop");
    g_object_set(be.src, "do-timestamp", TRUE, NULL);

    if(cmdArg.mode == TEST_MODE)
    {
        //GstPad *srcpad = gst_element_get_static_pad(be.src, "src");
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.src, NULL);
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.capsfilter, NULL);
        //gst_object_unref(srcpad);
    }


    if (csi == 0)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] %s : video4", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video4", NULL);
    }
    else if (csi == 1)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] %s : video3", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video3", NULL);
    }
    // g_object_set(ve.src, "device", g_strdup_printf("/dev/video%d", csi+3), NULL);

    // g_print("cmdArg.res[cmdArg.resMode].width:%d\n", cmdArg.res[cmdArg.resMode].width);
    // g_print("cmdArg.res[cmdArg.resMode].height:%d\n", cmdArg.res[cmdArg.resMode].height);
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, cmdArg.res[cmdArg.resMode].width * 2,
                                        "height", G_TYPE_INT, cmdArg.res[cmdArg.resMode].height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps, 1,
                                        NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (!be.bin || !be.src || !be.capsfilter || !be.teeCrop || !be.convert)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main element create error", _FILE_, __LINE__);
        return -1;
    }
    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.capsfilter, be.teeCrop, NULL);
    if (!gst_element_link_many(be.src, be.capsfilter, be.convert, be.teeCrop, NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
        return -1;
    }

    if (!gst_bin_add(GST_BIN(pipeline), be.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    return 1;
}