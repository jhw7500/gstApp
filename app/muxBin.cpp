/*
 *
 * Cantops muxBin.cpp support
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

#include "muxBin.h"

static void sink_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static void muxer_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

gboolean MuxBin::splitNow(gpointer data)
{
    //MuxBin* muxBin = MuxBin::getInstance();
    //MuxBin* muxBin = (MuxBin *)data;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    if(ch_enable_bit & 0x01)
    {
        g_print("ch0 split\n");
        g_signal_emit_by_name (me.sink[0], "split-now");
    }

    if(ch_enable_bit & (0x01 << 1))
    {
        g_print("ch1 split\n");
        g_signal_emit_by_name (me.sink[1], "split-now");
    }

    if(ch_enable_bit & (0x01 << 2))
    {
        g_print("ch2 split\n");
        g_signal_emit_by_name (me.sink[2], "split-now");
    }

    if(ch_enable_bit & (0x01 << 3))
    {
        g_print("ch3 split\n");
        g_signal_emit_by_name (me.sink[3], "split-now");  
    }

    //g_signal_emit_by_name (me.sink[0], "split-now");
    //g_signal_emit_by_name (getSinkElement(0), "split-now");

    return TRUE;
}

gchararray MuxBin::format_location(GstElement *sink, guint arg0, gpointer data)
{
    SinkData *info = (SinkData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //gint sec = g_date_time_get_second(datetime);
    gchar *date_str;

#if 1
    if(info->start_f == 0)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d firstSplitFlag", _FILE_, __LINE__, info->ch);
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, data);
        info->start_f = 1;
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    }
#endif

    //date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s%s_%s-ch%d.mp4", cmdArg.saveDir, PROGRAM_NAME, date_str, info->ch);
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

MuxBin* MuxBin::getInstance()
{
	static MuxBin instance;
	return &instance;
}

GstPad* MuxBin::getBinVideoSinkPad(guint8 ch)
{
    g_print("%s ch(%d)\n", __FUNCTION__, ch);
    return gst_element_get_static_pad(me.bin, g_strdup_printf("mux_video_sink_ch%d", ch));
}

GstPad* MuxBin::getBinAudioSinkPad(guint8 ch)
{
    g_print("%s ch(%d)\n", __FUNCTION__, ch);
    return gst_element_get_static_pad(me.bin, g_strdup_printf("mux_audio_sink_ch%d", ch));
}

gint MuxBin::addSink()
{

    return 0;
}

gint MuxBin::addPadAudioSink(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("mux_audio_sink_ch%d", ch), gst_element_get_request_pad(me.sink[ch], "audio_%u"))))
        g_error("error");
    
    return 0;
}


gint MuxBin::addPadVideoSink(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("mux_video_sink_ch%d", ch), gst_element_get_request_pad(me.sink[ch], "video_aux_%u"))))
        g_error("error");
    
    return 0;
}

gint MuxBin::addSink(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    ch_enable_bit |= (0x01 << ch);
    sinkData[ch].ch = ch;
    sinkData[ch].start_f = 0;

    me.sink[ch] = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", ch));
#if 1
    g_object_set(me.sink[ch], "max-size-time", 60*GST_SECOND, NULL);
    if(ch==0) g_object_set(me.sink[ch], "location", "test_ch0_%02d.mp4", NULL);
    if(ch==1) g_object_set(me.sink[ch], "location", "test_ch1_%02d.mp4", NULL);
    if(ch==2) g_object_set(me.sink[ch], "location", "test_ch2_%02d.mp4", NULL);
    if(ch==3) g_object_set(me.sink[ch], "location", "test_ch3_%02d.mp4", NULL);
#endif
    //g_signal_connect(me.sink[ch], "format-location", G_CALLBACK(format_location), &sinkData[ch]);
    g_signal_connect(me.sink[ch], "muxer-added", G_CALLBACK(muxer_added), &sinkData[ch]);
    g_signal_connect(me.sink[ch], "sink-added", G_CALLBACK(sink_added), &sinkData[ch]);

    gst_bin_add(GST_BIN(me.bin), me.sink[ch]);

    return 0;
}

gint MuxBin::init()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    me.bin = gst_bin_new("muxBin");
    //me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    
    //g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);

    if(!gst_bin_add(GST_BIN(pipeline), me.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    return 0;
}