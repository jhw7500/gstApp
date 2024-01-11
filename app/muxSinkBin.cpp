/*
 *
 * Cantops muxSinkBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
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
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    GstEventType eventType = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info));


    if (eventType == GST_EVENT_EOS) {
        g_print("ch%d Received EOS event on pad : %s\n", data->ch, GST_PAD_NAME(pad));
    }
    else if(eventType == GST_EVENT_TAG)
    {

    }
    else if(eventType == GST_EVENT_LATENCY)
    {
        GstClockTime latency;

        gst_event_parse_latency(event, &latency);
        g_print("ch%d latency : %" GST_TIME_FORMAT "\n", data->ch, GST_TIME_ARGS(latency));

        return TRUE;
    }
    else
    {
        g_print("ch%d Event Type: %s\n", data->ch, gst_event_type_get_name(eventType));
    }

    return GST_PAD_PROBE_OK;
}

static void split(gpointer data)
{
    MuxSinkElement *be = (MuxSinkElement *)data;
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    g_signal_emit_by_name (be->sink, "split-now");
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

    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    //g_signal_emit_by_name (be.sink, "split-at-running-time");
    g_signal_emit_by_name (be.sink, "split-after");
    //g_signal_emit_by_name (be.sink, "split-now");

    if(timer_en) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] split timer start ch : %d", _FILE_, __LINE__, muxSinkData.ch);
        g_timeout_add_seconds(cmdArg.duration, (GSourceFunc)split, &be);
    }

    return TRUE;
}

gint MuxSinkBin::getSplitSec()
{
    return muxSinkData.split_sec;
}

gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    MuxSinkData *info = (MuxSinkData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gint sec = g_date_time_get_second(datetime);
    gchar *date_str;
    static gboolean g_start_rec = FALSE;

#if 1
    if(info->start_f == 0)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d firstSplitFlag", _FILE_, __LINE__, info->ch);
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, data);
        info->start_f = 1;
        //date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        if(sec <= cmdArg.split_margin_sec) {
            datetime = g_date_time_add_seconds(datetime, -sec);
        }
        //else if(sec >= 60-margin_sec) {
        //    datetime = g_date_time_add_seconds(datetime, 60-sec);
        //}
    }
#endif
    info->split_sec = sec;
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s/%s_%s-ch%d.mp4", cmdArg.mntDir, cmdArg.ohtName, date_str, info->ch);
    
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    if(!g_start_rec)
    {
		FILE *fp = NULL;

		g_start_rec = TRUE;
		fp = fopen("/tmp/start_video_time", "wb");
		if(fp != NULL)
		{
            date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
			fwrite(date_str, sizeof(char), strlen(date_str), fp);
			fclose(fp);
		}
    }

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

MuxSinkBin* MuxSinkBin::getInstance()
{
	static MuxSinkBin instance;
	return &instance;
}

MuxSinkBin::MuxSinkBin()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    sinkAudioPad = NULL;
    sinkVideoPad = NULL;
    be.bin = NULL;
    muxSinkData.start_f = 0;
    muxSinkData.split_sec = 0;
}

MuxSinkBin::~MuxSinkBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
}

gboolean MuxSinkBin::addBinVideoSinkPad()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    
    sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "video_aux_%u"));
    if(!gst_element_add_pad(be.bin, sinkVideoPad))
        g_error("error");
    else gst_pad_add_probe(sinkVideoPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);
    
    return 0;
}

gboolean MuxSinkBin::addBinAudioSinkPad()
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "audio_%u"));
    if(!gst_element_add_pad(be.bin, sinkAudioPad))
        g_error("error");
    else gst_pad_add_probe(sinkAudioPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

    return 0;
}

GstPad* MuxSinkBin::getBinVideoSinkPad()
{
    //__LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    return sinkVideoPad;
}

GstPad* MuxSinkBin::getBinAudioSinkPad()
{
    //__LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    return sinkAudioPad;
}

gint MuxSinkBin::init(guint8 num)
{
    gint ret = 0;
    muxSinkData.ch = num;
    muxSinkData.start_f = 0;

    be.bin = gst_bin_new(g_strdup_printf("muxSinkBin%d", muxSinkData.ch));
    //me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    //g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);
    be.sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", muxSinkData.ch));
#if 0
    g_object_set(be.sink, "max-size-time", 60*GST_SECOND, NULL);
    if(ch==0) g_object_set(be.sink, "location", "test_ch0_%02d.mp4", NULL);
    if(ch==1) g_object_set(be.sink, "location", "test_ch1_%02d.mp4", NULL);
    if(ch==2) g_object_set(be.sink, "location", "test_ch2_%02d.mp4", NULL);
    if(ch==3) g_object_set(be.sink, "location", "test_ch3_%02d.mp4", NULL);
#endif
    g_object_set(be.sink, "max-size-time", GST_SECOND*60*cmdArg.duration, NULL);
    g_signal_connect(be.sink, "format-location", G_CALLBACK(format_location), &muxSinkData);
    g_signal_connect(be.sink, "muxer-added", G_CALLBACK(muxer_added), NULL);
    g_signal_connect(be.sink, "sink-added", G_CALLBACK(sink_added), NULL);

    if (!be.bin || !be.sink) {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux element create error", _FILE_, __LINE__);
        return ret;
    }
    
    ret = gst_bin_add(GST_BIN(be.bin), be.sink);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux add error in bin", _FILE_, __LINE__);
        return ret;
    }

    ret =gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] mux add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", ch), gst_element_get_request_pad(me.sink, "audio_%u"))))
        //g_error("error");

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", ch), gst_element_get_request_pad(me.sink, "video_aux_%u"))))
        //g_error("error");

    return ret;
}