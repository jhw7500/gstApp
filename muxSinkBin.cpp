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

static void sink_added(GstElement *sink, GstElement *arg0, gpointer data)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}


static void muxer_added(GstElement *splitmux, GstElement *muxer, gpointer user_data)
{
    const char *mode = (const char *)user_data;   // "mp4" | "qt" | "ts"
    const char *type = G_OBJECT_TYPE_NAME(muxer);

    __LOG(LOG_INFO, "[GST][%s:%d] muxer-added: mode=%s, type=%s, name=%s",
          _FILE_, __LINE__, mode, type, GST_ELEMENT_NAME(muxer));

    // qtmux(GstQTMux) / mp4mux(GstMP4Mux) / mpegtsmux(GstMpegTsMux)
    if (!g_strcmp0(mode, "qt")) {
        // qtmux 기반 fMP4/robust 목적
        if (g_str_has_suffix(type, "QTMux") || g_strrstr(type, "QTMux")) {
            g_object_set(muxer,
                // 1초 단위 fragment (ms)
                "fragment-duration", 1000,

                // robust muxing (ns) - 필요에 따라 튜닝
                "reserved-max-duration", (guint64)(60 * GST_MSECOND),
                "reserved-moov-update-period", (guint64)(1 * GST_MSECOND),

                // streamable은 플레이어/환경 따라 득실 있으니 기본은 false 추천
                "streamable", FALSE,
                NULL);
        } else {
            __LOG(LOG_WARNING, "[GST] qt mode인데 qtmux가 아닌 muxer가 붙음: %s", type);
        }
    }
    else if (!g_strcmp0(mode, "mp4")) {
        // mp4mux는 옵션이 적거나 이름이 다를 수 있음. 존재하는 속성만 set.
        // 속성이 없으면 g_object_set에서 경고가 날 수 있으니, 속성 존재 여부를 검사하는 습관이 좋음.
        // 여기선 대표적으로 streamable/fragment-duration이 있으면 적용하는 형태로 작성.
#if 0
        // fragment-duration이 지원되면 fMP4처럼 동작 가능
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "fragment-duration")) {
            g_object_set(muxer, "fragment-duration", 1000, NULL);
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "streamable")) {
            g_object_set(muxer, "streamable", FALSE, NULL);
        }

        // robust 관련은 mp4mux가 지원 안 할 수 있음(qtmux에 집중되어 있는 경우가 많음)
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "reserved-max-duration")) {
            g_object_set(muxer, "reserved-max-duration", (guint64)(60 * GST_SECOND), NULL);
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "reserved-moov-update-period")) {
            g_object_set(muxer, "reserved-moov-update-period", (guint64)GST_SECOND, NULL);
        }
#endif
    }
    else if (!g_strcmp0(mode, "ts")) {
        // TS는 보통 별도 옵션 적음. 필요시 alignment/PCR 관련 등만.
        // 여기서는 붙은 것만 확인.
        if (g_strrstr(type, "MpegTsMux") == NULL && g_strrstr(type, "MPEGTSMux") == NULL) {
            __LOG(LOG_WARNING, "[GST] ts mode : mpegtsmux? %s", type);
        }
    }
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
            __LOG(LOG_INFO, "[GST][%s:%d] ch%d Received EOS event on pad : %s", _FILE_, __LINE__, data->ch, GST_PAD_NAME(pad));
            is_interrupted = TRUE;
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
            __LOG(LOG_INFO, "[GST][%s:%d] ch%d stream start", _FILE_, __LINE__, data->ch);
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
    gchararray file_name;
    gchar *date_str;

    //info->split_msec = sec;
    info->split_msec = sec*1000 + usec/1000; 
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d, %d*1000+%d/1000=%d", _FILE_, __LINE__, info->ch, sec, msec, info->split_msec);

    if(cmdArg.audio_en && info->split_msec >= cmdArg.split_audio_min_msec) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] audio limit %d msec over..", _FILE_, __LINE__, cmdArg.split_audio_min_msec);
        //datetime = g_date_time_add_minutes(datetime, 1);
    }

    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    //date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    if(g_strcmp0(cmdArg.muxer, "ts") == 0)
        file_name = g_strdup_printf("%s/%s_%s-ch%d.ts", cmdArg.mntDir, cmdArg.ohtName, date_str, info->ch);
    else
        file_name = g_strdup_printf("%s/%s_%s-ch%d.mp4", cmdArg.mntDir, cmdArg.ohtName, date_str, info->ch);
    
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

void MuxSinkBin::handle_last_sample() {
    GstSample *sample;
    g_object_get(be.sink, "last-sample", &sample, NULL);

    if (sample) {
        GstCaps *caps = gst_sample_get_caps(sample);
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;

        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // 마지막 샘플 데이터 처리
            g_print("Last sample size: %zu\n", map.size);
            gst_buffer_unmap(buffer, &map);
        }

        if (caps) {
            gchar *caps_str = gst_caps_to_string(caps);
            g_print("Last sample caps: %s\n", caps_str);
            g_free(caps_str);
        }

        gst_sample_unref(sample);
    } else {
        g_print("No last sample available.\n");
    }
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
    
    sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("video_pad_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "video_aux_%u"));
    //sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.mux, "video_0"));
    //sinkAudioPad = gst_element_get_static_pad(be.mux, g_strdup_printf("sinkvideopad%d", muxSinkData.ch));
    gst_pad_add_probe(sinkVideoPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);
    
    return gst_element_add_pad(be.bin, sinkVideoPad);
}

gboolean MuxSinkBin::addBinAudioSinkPad()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("audio_pad_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.sink, "audio_%u"));
    //sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", muxSinkData.ch), gst_element_get_request_pad(be.mux, "audio_0"));
    //sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("sinkaudiopad%d", muxSinkData.ch), gst_element_get_static_pad(be.mux, g_strdup_printf("sinkaudiostaticpad%d", muxSinkData.ch)));
    gst_pad_add_probe(sinkAudioPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

    return gst_element_add_pad(be.bin, sinkAudioPad);
}

GstPad* MuxSinkBin::getBinVideoSinkPad()
{
    if(sinkVideoPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    return sinkVideoPad;
}

GstPad* MuxSinkBin::getBinAudioSinkPad()
{
    if(sinkAudioPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    return sinkAudioPad;
}

GstPad* MuxSinkBin::getBinQueuePad()
{
    if(sinkPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);

    return sinkPad;
}

gboolean MuxSinkBin::init(guint8 num)
{
    gboolean ret = 0;
    muxSinkData.ch = num;
    muxSinkData.start_f = 0;

    if(be.bin) return 0;

    be.bin = gst_bin_new(g_strdup_printf("muxSinkBin%d", muxSinkData.ch));
    //me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, muxSinkData.ch);
    //g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);
    be.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
    be.mp4mux = gst_element_factory_make("mp4mux", "mp4mux");
    be.tsmux = gst_element_factory_make("mpegtsmux", "mpegtsmux");
    be.qtmux = gst_element_factory_make("qtmux", "qtmux");
    be.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
    be.parse = gst_element_factory_make("h264parse", "h264parse");
    be.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");

    if (!be.bin || !be.sink || !be.mp4mux || !be.tsmux || !be.qtmux || !be.queue || !be.parse || !be.capsfilter) {
        __LOG(LOG_CRIT, "[GST][%s:%d] element create error", _FILE_, __LINE__);
        return ret;
    }
    gst_bin_add_many(GST_BIN(be.bin), be.parse, be.sink, be.capsfilter, NULL);
    ret =gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

#if 1
#define MUX_PROPERTIES "muxer"

    GstCaps *caps;
    g_object_set(be.sink, "async-finalize", FALSE, NULL);
    if(g_strcmp0(cmdArg.muxer, "ts") == 0) {
        g_object_set(be.sink, MUX_PROPERTIES, be.tsmux, NULL);
        caps = gst_caps_from_string("video/x-h264,stream-format=byte-stream,alignment=au");
    }
    else if(g_strcmp0(cmdArg.muxer, "qt") == 0)
    {
        g_object_set(be.sink, MUX_PROPERTIES, be.qtmux, NULL);
        caps = gst_caps_from_string("video/x-h264,stream-format=avc,alignment=au");
    }
    else {
        g_object_set(be.sink, MUX_PROPERTIES, be.mp4mux, NULL);
        caps = gst_caps_from_string("video/x-h264,stream-format=avc,alignment=au");
    }
    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    const char *mode = cmdArg.muxer; 
    g_signal_connect(be.sink, "muxer-added", G_CALLBACK(muxer_added), (gpointer)mode);

    ret = gst_element_link_many(be.parse, be.capsfilter, NULL);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] bin link error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    GstPad *muxsrcpad = gst_element_get_static_pad(be.capsfilter, "src");

    // splitmuxsink가 제공하는 pad 템플릿 이름이 환경마다 다를 수 있어서
    // 보통은 아래 중 하나가 맞음: "video" 또는 "video_%u"
    GstPad *muxsinkpad = gst_element_get_request_pad(be.sink, "video_aux_%u");
    // 만약 NULL이면 "video_%u"로 재시도
    if (!muxsinkpad) muxsinkpad = gst_element_get_request_pad(be.sink, "video");

    if (!muxsinkpad) {
        __LOG(LOG_CRIT, "[GST] splitmuxsink has no request pad video/video_aux_%%u");
        gst_object_unref(muxsrcpad);
        return FALSE;
    }

    if (gst_pad_link(muxsrcpad, muxsinkpad) != GST_PAD_LINK_OK) {
        __LOG(LOG_CRIT, "[GST] pad link capsfilter->splitmuxsink(video) failed");
        gst_object_unref(muxsrcpad);
        gst_object_unref(muxsinkpad);
        return FALSE;
    }

    gst_object_unref(muxsrcpad);
    gst_object_unref(muxsinkpad);
#endif

    guint64 duration;
    //duration = ((615)*GST_SECOND*cmdArg.duration)/10;
    duration = GST_SECOND*cmdArg.duration*60;
    __LOG(LOG_INFO, "[GST][%s:%d] file duration : %lld", _FILE_, __LINE__, duration);
    
    g_object_set(be.sink, "max-size-time", duration, NULL);
    //g_object_set(be.sink, "reserved-max-duration", 3000000000000000000, NULL);
    //g_object_set(be.sink, "reserved-moov-update-period", 1000000000, NULL);
    //g_object_set(be.sink, "use-robust-muxing", TRUE, NULL);
    g_object_set(be.queue, "max-size-time", GST_SECOND/2, "max-size-buffers", cmdArg.fps[STREAM_REC][muxSinkData.ch]/2, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.parse, "config-interval", 1, NULL);
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

    //g_object_set(be.mp4mux, "latency", 100000, NULL);
    //g_object_set(be.mp4mux, "min-upstream-latency", 1000, NULL);
    //g_object_set(be.mp4mux, "fragment-duration", 1000, NULL);
#if 0
    g_object_set(be.mp4mux, "dts-method", 1, NULL);
    g_object_set(be.mp4mux, "emit-signals", 0, NULL);
    g_object_set(be.mp4mux, "faststart", 0, NULL);
    g_object_set(be.mp4mux, "faststart-file", "/tmp/qtmux-1870964074", NULL);
    g_object_set(be.mp4mux, "force-chunks", 0, NULL);
    g_object_set(be.mp4mux, "force-create-timecode-trak", 0, NULL);
    g_object_set(be.mp4mux, "fragment-duration", 0, NULL);
    g_object_set(be.mp4mux, "interleave-bytes", 0, NULL);
    g_object_set(be.mp4mux, "interleave-time", 250000000, NULL);
    g_object_set(be.mp4mux, "latency", 0, NULL);
    g_object_set(be.mp4mux, "max-raw-audio-drift", 40000000, NULL);
    g_object_set(be.mp4mux, "min-upstream-latency", 0, NULL);
    g_object_set(be.mp4mux, "moov-recovery-file", NULL, NULL);
    g_object_set(be.mp4mux, "movie-timescale", 0, NULL);
    g_object_set(be.mp4mux, "name", "mp4mux0", NULL);
    //g_object_set(be.mp4mux, "parent", 0, NULL);
    g_object_set(be.mp4mux, "presentation-time", 1, NULL);
    g_object_set(be.mp4mux, "reserved-bytes-per-sec", 550, NULL);
    g_object_set(be.mp4mux, "reserved-max-duration", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "reserved-moov-update-period", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "reserved-prefill", 0, NULL);
    g_object_set(be.mp4mux, "start-gap-threshold", 0, NULL);
    g_object_set(be.mp4mux, "start-time", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "start-time-selection", 0, NULL);
    g_object_set(be.mp4mux, "streamable", 0, NULL);
    g_object_set(be.mp4mux, "trak-timescale", 0, NULL);
#endif

    
    g_signal_connect(be.sink, "format-location", G_CALLBACK(format_location), &muxSinkData);
    g_signal_connect(be.sink, "sink-added", G_CALLBACK(sink_added), NULL);

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", ch), gst_element_get_request_pad(me.sink, "audio_%u"))))
        //g_error("error");

    //if(!gst_element_add_pad(me.bin, gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", ch), gst_element_get_request_pad(me.sink, "video_aux_%u"))))
        //g_error("error");

    sinkPad = gst_ghost_pad_new("srcpad", gst_element_get_static_pad(be.parse, "sink"));
    gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

    ret = gst_element_add_pad(be.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
        return ret;
    }

    return ret;
}