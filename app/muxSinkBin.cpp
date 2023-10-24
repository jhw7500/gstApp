/*
 *
 * Cantops muxSinkBin.cpp support
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

#include "muxSinkBin.h"

static void sink_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static void muxer_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static gboolean handle_eos_event(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
    MuxSinkData *data = (MuxSinkData *)user_data;
    GstEventType event = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info));


    if (event == GST_EVENT_EOS) {
        g_print("Received EOS event on pad[%d] : %s\n", data->ch, GST_PAD_NAME(pad));
    }
    else if(event == GST_EVENT_TAG)
    {

    }
    else
    {
        g_print("Event Type: %s\n", gst_event_type_get_name(event));
    }

    return GST_PAD_PROBE_OK;
}

static void split(gpointer data)
{
    MuxSinkElement *me = (MuxSinkElement *)data;
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    g_signal_emit_by_name (me->sink, "split-now");
}

guint8 MuxSinkBin::getStartFlag()
{
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    return muxSinkData.start_f;
}

gboolean MuxSinkBin::splitNow(gpointer data, gboolean timer_en)
{
    //MuxBin* muxBin = MuxBin::getInstance();
    //MuxBin* muxBin = (MuxBin *)data;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    g_signal_emit_by_name (me.sink, "split-now");

    if(timer_en) g_timeout_add_seconds(cmdArg.duration, (GSourceFunc)split, &me);

    return TRUE;
}

gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    MuxSinkData *info = (MuxSinkData *)data;
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
    gchararray file_name = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, cmdArg.ohtName, date_str, info->ch);
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

MuxSinkBin* MuxSinkBin::getInstance()
{
	static MuxSinkBin instance;
	return &instance;
}

gboolean MuxSinkBin::addBinVideoSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    
    sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", muxSinkData.ch), gst_element_get_request_pad(me.sink, "video_aux_%u"));
    if(!gst_element_add_pad(me.bin, sinkVideoPad))
        g_error("error");
    else gst_pad_add_probe(sinkVideoPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);
    
    return 0;
}

gboolean MuxSinkBin::addBinAudioSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", muxSinkData.ch), gst_element_get_request_pad(me.sink, "audio_%u"));
    if(!gst_element_add_pad(me.bin, sinkAudioPad))
        g_error("error");
    else gst_pad_add_probe(sinkAudioPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

    return 0;
}

GstPad* MuxSinkBin::getBinVideoSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    return sinkVideoPad;
}

GstPad* MuxSinkBin::getBinAudioSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    return sinkAudioPad;
}

gint MuxSinkBin::init(guint8 num)
{
    sinkVideoPad = NULL;
    sinkAudioPad = NULL;
    muxSinkData.ch = num;
    muxSinkData.start_f = 0;
    me.bin = gst_bin_new(g_strdup_printf("muxBinSink%d", muxSinkData.ch));
    //me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    //g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);
    me.sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", muxSinkData.ch));
#if 0
    g_object_set(me.sink, "max-size-time", 60*GST_SECOND, NULL);
    if(ch==0) g_object_set(me.sink, "location", "test_ch0_%02d.mp4", NULL);
    if(ch==1) g_object_set(me.sink, "location", "test_ch1_%02d.mp4", NULL);
    if(ch==2) g_object_set(me.sink, "location", "test_ch2_%02d.mp4", NULL);
    if(ch==3) g_object_set(me.sink, "location", "test_ch3_%02d.mp4", NULL);
#endif

    g_signal_connect(me.sink, "format-location", G_CALLBACK(format_location), &muxSinkData);
    g_signal_connect(me.sink, "muxer-added", G_CALLBACK(muxer_added), NULL);
    g_signal_connect(me.sink, "sink-added", G_CALLBACK(sink_added), NULL);

    if (!me.bin || !me.sink)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux sink element create error", _FILE_, __LINE__);
        return -1;
    }

    if(!gst_bin_add(GST_BIN(me.bin), me.sink))
        g_error("error");

    if(!gst_bin_add(GST_BIN(pipeline), me.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", ch), gst_element_get_request_pad(me.sink, "audio_%u"))))
        //g_error("error");

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", ch), gst_element_get_request_pad(me.sink, "video_aux_%u"))))
        //g_error("error");

    return 0;
}