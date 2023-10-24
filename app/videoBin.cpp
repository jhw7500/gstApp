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

gint VideoBin::addBinRtspSrcPad(ChannelNum ch)
{
    if(ve.crop[ch%2] = NULL)
        srcRtspPad = gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch%2), gst_element_get_request_pad(ve.teeCrop, "src_%u"));
    else
        srcRtspPad = gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch%2), gst_element_get_request_pad(ve.tee[ch%2], "src_%u"));

    if (!gst_element_add_pad(ve.bin, srcRtspPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    return 0;
}

gint VideoBin::addBinRecordSrcPad(ChannelNum ch)
{
    if(ve.crop[ch%2] = NULL)
        srcRecordPad = gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch%2), gst_element_get_request_pad(ve.teeCrop, "src_%u"));
    else
        srcRecordPad = gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch%2), gst_element_get_request_pad(ve.tee[ch%2], "src_%u"));

    if (!gst_element_add_pad(ve.bin, srcRecordPad))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] %s ch[%d] error", _FILE_, __LINE__, __FUNCTION__, ch);
        return -1;
    }

    return 0;
}

VideoBin::VideoBin()
{
    ve.bin = NULL;
}

VideoBin::~VideoBin()
{
    
}

gint VideoBin::addCrop(CropDir dir)
{
    //g_print("csi:%d, dir:%d\n", csi, dir);
    
    ve.crop[dir] = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", dir));
    ve.tee[dir] = gst_element_factory_make("tee", g_strdup_printf("tee%d", dir));
    ve.overlay[dir] = gst_element_factory_make("timeoverlay", g_strdup_printf("overaly%d", dir));
    ve.queue[dir] = gst_element_factory_make("queue", g_strdup_printf("queue%d", dir));

    if (dir % 2 == 0)
        g_object_set(ve.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
    else
        g_object_set(ve.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);

    if (!ve.crop[dir] || !ve.tee[dir] || !ve.overlay[dir] || !ve.queue[dir])
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_, __LINE__, dir);
        return -1;
    }

    g_object_set(ve.overlay[dir], "valignment", 2, NULL);
    g_object_set(ve.overlay[dir], "halignment", 0, NULL);
    g_object_set(ve.overlay[dir], "font-desc", "Times New Roman Italic, 16", NULL);
    g_object_set(ve.overlay[dir], "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(ve.overlay[dir], "show-times-as-dates", TRUE, NULL);
    g_object_set(ve.overlay[dir], "datetime-epoch", g_date_time_new_now_local(), NULL);
    g_object_set(ve.queue[dir], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, cmdArg.res[cmdArg.resMode].width*2,
                                        "height", G_TYPE_INT, cmdArg.res[cmdArg.resMode].height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps, 1,
                                        NULL);

    g_object_set(ve.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(ve.bin), ve.crop[dir], ve.tee[dir], ve.overlay[dir], ve.queue[dir], NULL);
    if (!gst_element_link_many(ve.teeCrop, ve.queue[dir], ve.crop[dir], ve.tee[dir], NULL))
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
    //RecordBin recordBin[4];
    gint ret = 0;
    //guint8 chCnt = 0;
    csi = num;
    //guint8 ch;
    //guint i = 0;

    if(ve.bin != NULL)
    {
        //__LOG(LOG_NOTICE, "[GST][%s:%d] %s(%d) already init", _FILE_, __LINE__, __FUNCTION__, csi);
        return 0;
    }

    do
    {
        //ve.bins
        //__LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, csi);

        ve.bin = gst_bin_new(g_strdup_printf("videoBin%d", csi));
        ve.src = gst_element_factory_make("v4l2src", "src");
        ve.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
        ve.capsfilter = gst_element_factory_make("capsfilter", "caps");
        ve.teeCrop = gst_element_factory_make("tee", "teeCrop");
        ve.crop[0] = NULL;
        ve.crop[1] = NULL;

        g_object_set(ve.src, "do-timestamp", TRUE, NULL);

        if(csi == 0) {
             __LOG(LOG_NOTICE, "[GST][%s:%d] %s : video4", _FILE_, __LINE__, __FUNCTION__);
            g_object_set(ve.src, "device", "/dev/video4", NULL);
        }
        else if(csi == 1) {
             __LOG(LOG_NOTICE, "[GST][%s:%d] %s : video3", _FILE_, __LINE__, __FUNCTION__);
            g_object_set(ve.src, "device", "/dev/video3", NULL);
        }
        //g_object_set(ve.src, "device", g_strdup_printf("/dev/video%d", csi+3), NULL);

        //g_print("cmdArg.res[cmdArg.resMode].width:%d\n", cmdArg.res[cmdArg.resMode].width);
        //g_print("cmdArg.res[cmdArg.resMode].height:%d\n", cmdArg.res[cmdArg.resMode].height);
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, cmdArg.res[cmdArg.resMode].width,
                                        "height", G_TYPE_INT, cmdArg.res[cmdArg.resMode].height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps, 1,
                                        NULL);

        g_object_set(ve.capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);

        if (!ve.bin || !ve.src || !ve.capsfilter || !ve.teeCrop || !ve.convert)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main element create error", _FILE_, __LINE__);
            return -1;
        }
        gst_bin_add_many(GST_BIN(ve.bin), ve.src, ve.convert, ve.capsfilter, ve.teeCrop, NULL);
        if (!gst_element_link_many(ve.src, ve.capsfilter, ve.convert, ve.teeCrop, NULL))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
            return -1;
        }

#if 0
        do
        {
            ch = csi*2+chCnt;
            g_print("ch:%d\n", ch);
            ve.crop[chCnt] = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", chCnt));
            ve.tee[chCnt] = gst_element_factory_make("tee", g_strdup_printf("tee%d", chCnt));
            ve.overlay[chCnt] = gst_element_factory_make("timeoverlay", g_strdup_printf("overaly%d", chCnt));

            if(chCnt%2 == 0)
                g_object_set(ve.crop[chCnt], "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);
            else
                g_object_set(ve.crop[chCnt], "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);

            if (!ve.crop[chCnt] || !ve.tee[chCnt] || !ve.overlay[chCnt])
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_, __LINE__, chCnt);
                return -1;
            }

            gst_bin_add_many(GST_BIN(ve.bin), ve.crop[chCnt], ve.tee[chCnt], ve.overlay[chCnt], NULL);
            if (!gst_element_link_many(ve.teeCrop, ve.crop[chCnt], ve.overlay[chCnt], ve.tee[chCnt], NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] crop [%d] link err", _FILE_, __LINE__, chCnt);
                return -1;
            }

            if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch), gst_element_get_request_pad(ve.tee[chCnt], "src_%u"))))
                g_error("error0");
            else
                g_message("videoBin_record_src_ch%d", ch);
            if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch), gst_element_get_request_pad(ve.tee[chCnt], "src_%u"))))
                g_error("error1");
            else
                g_message("videoBin_rtsp_src_ch%d", ch);

        } while(++chCnt < 2);
#endif

#if 0
        ve.cropL = gst_element_factory_make("videocrop", "cropL");
        ve.teeL = gst_element_factory_make("tee", "teeL");
        g_object_set(ve.cropL, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
        if (!ve.cropL || !ve.teeL)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video crop left create error", _FILE_, __LINE__);
            return -1;
        }
        gst_bin_add_many(GST_BIN(ve.bin), ve.cropL, ve.teeL, NULL);
        if (!gst_element_link_many(ve.teeCrop, ve.cropL, ve.teeL, NULL))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] crop left link err", _FILE_, __LINE__);
            return -1;
        }

        if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", csi*2), gst_element_get_request_pad(ve.teeL, "src_%u"))))
            g_error("error0");
        else
            g_message("videoBin_record_src_ch%d", csi*2);
        if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", csi*2), gst_element_get_request_pad(ve.teeL, "src_%u"))))
            g_error("error1");
        else
            g_message("videoBin_rtsp_src_ch%d", csi*2);


        ve.cropR = gst_element_factory_make("videocrop", "cropR");
        ve.teeR = gst_element_factory_make("tee", "teeR");
        g_object_set(ve.cropR, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);
        if (!ve.cropR || !ve.teeR)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video crop right create error", _FILE_, __LINE__);
            return -1;
        }
        gst_bin_add_many(GST_BIN(ve.bin), ve.cropR, ve.teeR, NULL);
        if (!gst_element_link_many(ve.teeCrop, ve.cropR, ve.teeR, NULL))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] crop right link err", _FILE_, __LINE__);
            return -1;
        }
        if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", csi*2+1), gst_element_get_request_pad(ve.teeR, "src_%u"))))
            g_error("error2");
        else
            g_message("videoBin_record_src_ch%d", csi*2+1);
        if(!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", csi*2+1), gst_element_get_request_pad(ve.teeR, "src_%u"))))
            g_error("error3");
        else
            g_message("videoBin_rtsp_src_ch%d", csi*2+1);

#endif
        if(!gst_bin_add(GST_BIN(pipeline), ve.bin))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video bin add error in pipeline", _FILE_, __LINE__);
            return -1;
        }

        //gst_element_get_request_pad(ve.tee, "src_%u");

        //RecordBin* recordBin0 = RecordBin::getInstance();
#if 0
        
        recordBin[csi*2].init(csi*2);

        if(gst_pad_link(getRecordBinPad(csi*2), recordBin[csi*2].getBinPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Record pad link err", _FILE_, __LINE__);
            return -1;
        }
        recordBin[csi*2+1].init(csi*2+1);

        if(gst_pad_link(getRecordBinPad(csi*2+1), recordBin[csi*2+1].getBinPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Record pad link err", _FILE_, __LINE__);
            return -1;
        }
#endif

#if 0
        GstElement *sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
        GstElement *enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
        GstElement *parse = gst_element_factory_make("h264parse", "h264parse");
        g_object_set(sink, "max-size-time", 10*GST_SECOND, NULL);

        if(i==0) g_object_set(sink, "location", "testL%02d.mp4", NULL);
        if(i==1) g_object_set(sink, "location", "testR%02d.mp4", NULL);

        g_object_set(enc, "bitrate", 4096, NULL);

        gst_bin_add_many(GST_BIN(ve.bin), sink, enc, parse, NULL);

        if(!gst_element_link_many(ve.teeL, enc, parse, sink, NULL))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] sink link err", _FILE_, __LINE__);
            return -1;
        }
#endif


    } while(0);

    return ret;

err:
    return -1;
}