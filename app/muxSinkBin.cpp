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

    __LOG(LOG_INFO, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__, data->ch, gst_event_type_get_name(eventType));
    switch(eventType)
    {
        case GST_EVENT_EOS:
        {
            __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d Received EOS event on pad : %s", _FILE_, __LINE__, data->ch, GST_PAD_NAME(pad));
            break;
        }
        case GST_EVENT_TAG:
        {
            //GstTagList *taglist;
            //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d GST_EVENT_TAG", _FILE_, __LINE__, data->ch);
            //gst_event_parse_tag (event, &taglist);
            //gst_tag_list_foreach (taglist, print_tag, NULL);
            //gst_tag_list_free (taglist);
            break;
        }
        case GST_EVENT_LATENCY:
        {
            GstClockTime latency;
            gst_event_parse_latency(event, &latency);
            __LOG(LOG_INFO, "[GST][%s:%d] ch%d latency : %" GST_TIME_FORMAT "", _FILE_, __LINE__, data->ch, GST_TIME_ARGS(latency));
            break;
        }
        case GST_EVENT_CAPS:
        {
            //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__, data->ch, gst_event_type_get_name(eventType));
            //GstCaps *caps;
            //gst_event_parse_caps (event, &caps);
            //GstStructure *structure = gst_caps_get_structure (caps, 0);
            //__LOG(LOG_NOTICE, "[GST][%s:%d] caps[%d] : %s", _FILE_, __LINE__, data->ch, gst_caps_to_string(caps));
            //gst_structure_free(structure);
            //gst_caps_unref(caps);
            //if (!(res = gst_ffmpegmux_setcaps (pad, caps))) goto beach;
            break;
        }
        case GST_EVENT_SEGMENT:
        {
            //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__, data->ch, gst_event_type_get_name(eventType));
            break;
        }
        case GST_EVENT_STREAM_START:
        {
            data->start_f = 1;
            //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d stream start", _FILE_, __LINE__, data->ch);
            break;
        }
        default:
        {
            //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__, data->ch, gst_event_type_get_name(eventType));
            break;
        }
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

void MuxSinkBin::setSplitMsec(gint msec)
{
    muxSinkData.split_msec = msec;
}

gint MuxSinkBin::getSplitMsec()
{
    return muxSinkData.split_msec;
}

gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    MuxSinkData *info = (MuxSinkData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gint sec = g_date_time_get_second(datetime);
    gint usec = g_date_time_get_microsecond(datetime);
    gchar *date_str;

    //info->split_msec = sec;
    info->split_msec = sec*1000 + usec/1000;
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d, %d*1000+%d/1000=%d", _FILE_, __LINE__, info->ch, sec, msec, info->split_msec);
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    gchararray file_name = g_strdup_printf("%s/%s_%s-ch%d.mp4", cmdArg.mntDir, cmdArg.ohtName, date_str, info->ch);
    
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

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
    muxSinkData.split_msec = 0;
}

MuxSinkBin::~MuxSinkBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
}

gboolean MuxSinkBin::addBinVideoSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    
    sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "video_aux_%u"));
    gst_pad_add_probe(sinkVideoPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);
    
    return gst_element_add_pad(be.bin, sinkVideoPad);
}

gboolean MuxSinkBin::addBinAudioSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "audio_%u"));
    gst_pad_add_probe(sinkAudioPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

    return gst_element_add_pad(be.bin, sinkAudioPad);
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

gboolean MuxSinkBin::init(guint8 num)
{
    gboolean ret = 0;
    muxSinkData.ch = num;
    muxSinkData.start_f = 0;

    be.bin = gst_bin_new(g_strdup_printf("muxSinkBin%d", muxSinkData.ch));
    //me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    //g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);
    be.sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", muxSinkData.ch));
    be.mp4mux = gst_element_factory_make("mp4mux", g_strdup_printf("mp4mux%d", muxSinkData.ch));
#if 0
    g_object_set(be.sink, "max-size-time", 60*GST_SECOND, NULL);
    if(ch==0) g_object_set(be.sink, "location", "test_ch0_%02d.mp4", NULL);
    if(ch==1) g_object_set(be.sink, "location", "test_ch1_%02d.mp4", NULL);
    if(ch==2) g_object_set(be.sink, "location", "test_ch2_%02d.mp4", NULL);
    if(ch==3) g_object_set(be.sink, "location", "test_ch3_%02d.mp4", NULL);
#endif
    g_object_set(be.sink, "max-size-time", GST_SECOND*60*cmdArg.duration, NULL);
    //g_object_set(be.sink, "reserved-max-duration", 3000000000000000000, NULL);
    //g_object_set(be.sink, "reserved-moov-update-period", 1000000000, NULL);
    //g_object_set(be.sink, "use-robust-muxing", TRUE, NULL);

#if 0
    GstStructure *muxer_properties = gst_structure_new("application/x-gst-mp4mux",
                                         "reserved-moov-update-period", G_TYPE_UINT64, 1000000000,
                                         "reserved-max-duration", G_TYPE_UINT64, 3000000000000000000, 
                                         "faststart", G_TYPE_BOOLEAN, TRUE,
                                         NULL);
#endif
#if 0
    GstStructure *muxer_properties = gst_structure_new_empty("muxer-properties");
    gst_structure_set(muxer_properties, "latency", G_TYPE_UINT64, 1000, "faststart", G_TYPE_BOOLEAN, TRUE, \
                "reserved-moov-update-period", G_TYPE_UINT64, 1000000000, "reserved-max-duration", G_TYPE_UINT64, 3000000000000000000, NULL);

    g_object_set(be.sink, "muxer-properties", muxer_properties, NULL);
    gst_structure_free(muxer_properties);
#endif
    g_object_set(be.mp4mux, "reserved-moov-update-period", 10000000000, NULL);
    //g_object_set(be.mp4mux, "latency", 100000, NULL);
    //g_object_set(be.mp4mux, "min-upstream-latency", 1000, NULL);
    //g_object_set(be.mp4mux, "fragment-duration", 1000, NULL);
    g_object_set(be.sink, "muxer", be.mp4mux, NULL);

    g_signal_connect(be.sink, "format-location", G_CALLBACK(format_location), &muxSinkData);
    g_signal_connect(be.sink, "muxer-added", G_CALLBACK(muxer_added), NULL);
    g_signal_connect(be.sink, "sink-added", G_CALLBACK(sink_added), NULL);

    if (!be.bin || !be.sink || !be.mp4mux) {
        __LOG(LOG_CRIT, "[GST][%s:%d] element create error", _FILE_, __LINE__);
        return ret;
    }
    
    ret = gst_bin_add(GST_BIN(be.bin), be.sink);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink add error in bin", _FILE_, __LINE__);
        return ret;
    }

    ret =gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", ch), gst_element_get_request_pad(me.sink, "audio_%u"))))
        //g_error("error");

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", ch), gst_element_get_request_pad(me.sink, "video_aux_%u"))))
        //g_error("error");

    return ret;
}