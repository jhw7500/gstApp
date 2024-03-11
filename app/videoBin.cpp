/*
 *
 * Cantops videoBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "videoBin.h"

static void prepare_format(GstElement *object, gint arg0, GstCaps *caps, gpointer data)
{
    guint8 *csi = (guint8 *)data;
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s [%d]", _FILE_, __LINE__, __FUNCTION__, *csi);
    __LOG(LOG_INFO, "[GST][%s:%d] csi%d caps : %s", _FILE_, __LINE__, *csi, gst_caps_to_string(caps));
}

void VideoBin::getIoMode()
{
    gint ioMode;

    g_object_get(be.src, "io-mode", &ioMode, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] csi%d get io-mode : %d", _FILE_, __LINE__, csi, ioMode);
    g_print("csi%d get io-mode : %d\n", csi, ioMode);
}

void VideoBin::setIoMode(guint16 data)
{
    gint ioMode;

    g_object_set(be.src, "io-mode", data, NULL);
    g_object_get(be.src, "io-mode", &ioMode, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] csi%d set io-mode : %d", _FILE_, __LINE__, csi, ioMode);
    g_print("csi%d set io-mode : %d\n", csi, ioMode);
}

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
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcRtspPad = gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));

    if (!gst_element_add_pad(be.bin, srcRtspPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    return 0;
}

gint VideoBin::addBinRecordSrcPad(ChannelNum ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcRecordPad = gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));

    if (!gst_element_add_pad(be.bin, srcRecordPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    if(cmdArg.levelMode == MODE_TEST)
    {
        gst_pad_add_probe(srcRecordPad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.bin, NULL);
    }

    return 0;
}

gint VideoBin::addBinCaptureSrcPad(ChannelNum ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcCapturePad = gst_ghost_pad_new(g_strdup_printf("videoBin_capture_src_ch%d", ch%2), gst_element_get_request_pad(be.teeCrop, "src_%u"));

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
    srcRecordPad = NULL;
    srcRtspPad = NULL;
}

VideoBin::~VideoBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, csi);
}

gint VideoBin::addCrop(CropDir dir)
{
    gint ret = 0;
    //g_print("csi:%d, dir:%d\n", csi, dir);
    __LOG(LOG_NOTICE, "[GST][%s:%d] video crop csi : %d, dir : %d", _FILE_, __LINE__, csi, dir);

    be.crop[dir] = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", dir));
    be.tee[dir] = gst_element_factory_make("tee", g_strdup_printf("tee%d", dir));
    be.overlay[dir] = gst_element_factory_make("timeoverlay", g_strdup_printf("overaly%d", dir));
    be.queue[dir] = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", dir));
    be.convert2[dir] = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("convert%d", dir));

    if (dir % 2 == 0)
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);
        //g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
    else
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
        //g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);

    if (!be.crop[dir] || !be.tee[dir] || !be.overlay[dir] || !be.queue[dir] || !be.convert2[dir]) {
        __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_, __LINE__, dir);
        return ret;
    }

    g_object_set(be.overlay[dir], "valignment", 2, NULL);
    g_object_set(be.overlay[dir], "halignment", 0, NULL);
    g_object_set(be.overlay[dir], "font-desc", "Times New Roman Italic, 16", NULL);
    g_object_set(be.overlay[dir], "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(be.overlay[dir], "show-times-as-dates", TRUE, NULL);
    g_object_set(be.overlay[dir], "datetime-epoch", g_date_time_new_now_local(), NULL);
    g_object_set(be.queue[dir], "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.main_fps[csi], "leaky", 1, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[csi], 1,
                                        NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(be.bin), be.crop[dir], be.tee[dir], be.overlay[dir], be.queue[dir], be.convert2[dir], NULL);
    
    if(cmdArg.overlay_en) ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.overlay[dir], be.convert2[dir], be.tee[dir], NULL);
    else ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.convert2[dir], be.tee[dir], NULL);

    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] crop [%d] link err", _FILE_, __LINE__, dir);
        return ret;
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

    return ret;
}

gint VideoBin::init(CsiNum num, gboolean crop_en)
{
    GstCaps *caps;
    gint ret = 0;
    csi = num;

    if(be.bin != NULL)
    {
        //__LOG(LOG_NOTICE, "[GST][%s:%d] %s(%d) already init", _FILE_, __LINE__, __FUNCTION__, csi);
        return ret;
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d] crop %s", _FILE_, __LINE__, __FUNCTION__, csi, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("videoBin%d", csi));
    be.src = gst_element_factory_make("v4l2src", "src");
    be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    be.capsfilter = gst_element_factory_make("capsfilter", "caps");
    be.teeCrop = gst_element_factory_make("tee", "teeCrop");
    be.queue_main = gst_element_factory_make(QUEUE_TYPE, "queue_main");

    if (!be.bin || !be.src || !be.capsfilter || !be.teeCrop || !be.convert || !be.queue_main)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main element create error", _FILE_, __LINE__);
        return ret;
    }
    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.capsfilter, be.teeCrop, be.queue_main, NULL);

    g_object_set(be.src, "io-mode", cmdArg.ioMode, NULL);   //0:auto, 1:rw, 2:mmap, 3:userptr, 4:dmabuf, 5:dmabuf-import
    g_object_set(be.src, "do-timestamp", TRUE, NULL);
    g_signal_connect(be.src, "prepare-format", G_CALLBACK(prepare_format), &csi);
    g_object_set(be.queue_main, "max-size-time", GST_SECOND, "max-size-buffers", cmdArg.main_fps[csi], "leaky", LEAKY_DOWNSTREAM, NULL);

    if(cmdArg.levelMode == MODE_TEST)
    {
        //GstPad *srcpad = gst_element_get_static_pad(be.src, "src");
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.src, NULL);
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.capsfilter, NULL);
        //gst_object_unref(srcpad);
    }

    if (csi == 0)
    {
        __LOG(LOG_INFO, "[GST][%s:%d] %s : video4", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video4", NULL);
    }
    else if (csi == 1)
    {
        __LOG(LOG_INFO, "[GST][%s:%d] %s : video3", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video3", NULL);
    }

    if(crop_en)
    {
        caps = gst_caps_new_simple("video/x-raw",
                                    "width", G_TYPE_INT, cmdArg.width*2,
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[csi], 1,
                                    NULL);
        ret = gst_element_link_many(be.src, be.convert, be.capsfilter, be.teeCrop, NULL);
        if (!ret)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
            return ret;
        }
    }
    else
    {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                    "width", G_TYPE_INT, cmdArg.width,
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[csi], 1,
                                    NULL);
        ret = gst_element_link_many(be.src, be.capsfilter, be.teeCrop, NULL);
        if (!ret)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
            return ret;
        }
    }

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if (!ret)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    return ret;
}