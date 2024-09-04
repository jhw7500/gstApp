/*
 *
 * Cantops audioBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "audioBin.h"

AudioBin* AudioBin::getInstance()
{
	static AudioBin instance;
	return &instance;
}

AudioBin::AudioBin()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    be.bin = NULL;
}

// RecordBin 클래스의 소멸자 정의
AudioBin::~AudioBin()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

GstPad* AudioBin::getBinSrcPad(guint8 ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s : %d", _FILE_, __LINE__, __FUNCTION__, ch);
    
    //return gst_element_get_static_pad(be.bin, g_strdup_printf("audio_src_ch%d", ch));;
    return audioSrcPad;
}

gboolean AudioBin::addBinSrcPad(guint8 ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    audioSrcPad = gst_ghost_pad_new(g_strdup_printf("audio_rec_src_ch%d", ch), gst_element_get_request_pad(be.tee, "src_%u"));
    //audioSrcPad = gst_ghost_pad_new(g_strdup_printf("audio_src_ch%d", ch), gst_element_get_static_pad(be.queue, "src"));
    
    return gst_element_add_pad(be.bin, audioSrcPad);
}

gboolean AudioBin::init()
{
    gboolean ret = 0;
    GstCaps *audio_caps;
    be.bin = gst_bin_new("audioBin");

    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    be.src = gst_element_factory_make("alsasrc", "audiosrc");
    be.convert = gst_element_factory_make("audioconvert", "audioconvert");
    be.resample = gst_element_factory_make("audioresample", "audioresample");
    be.enc = gst_element_factory_make("lamemp3enc", "audioencoder");
    be.queue = gst_element_factory_make("queue", "audioqueue");
    be.queue2 = gst_element_factory_make("queue", "audioqueue2");
    be.capsfilter = gst_element_factory_make("capsfilter", "audiocaps");
    be.parse = gst_element_factory_make("mpegaudioparse", "audioparse");
    be.tee = gst_element_factory_make("tee", "audiotee");
    be.rate = gst_element_factory_make("audiorate", "audiorate");
    //be.filter = gst_element_factory_make("webrtcdsp", "webrtcdsp");
    //be.jitter = gst_element_factory_make("audiolatency", "jitter");

    g_object_set(be.src, "device", "plughw:0,0", NULL);
    //g_object_set(be.src, "provide-clock", TRUE, NULL);
    //g_object_set(be.src, "latency-time", 200000, NULL);
    //g_object_set(be.src, "slave-method", 0, NULL);
    g_object_set(be.enc, "target", 1, "bitrate", 192, NULL);
    //g_object_set(be.enc, "bitrate", 128, NULL);
    //g_object_set(be.jitter, "sleep-time", 1000, NULL);
    //g_object_set(be.jitter, "print-latency", TRUE, NULL);
    g_object_set(be.queue, "max-size-time", GST_SECOND, "max-size-buffers", 0, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND, "max-size-buffers", 0, "leaky", LEAKY_DOWNSTREAM, NULL);

    audio_caps = gst_caps_new_simple("audio/x-raw",
                                "format", G_TYPE_STRING, "S24LE",
                                "rate", G_TYPE_INT, 48000,
                                "channels", G_TYPE_INT, 1,
                                NULL);

    if(!be.bin || !be.src || !be.convert || !be.resample || !be.enc || !be.queue || !be.capsfilter || !be.parse || !be.tee || !be.rate || !be.queue2) {
        __LOG(LOG_CRIT, "[GST][%s:%d] element create error", _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.resample, be.rate, be.queue, be.enc, be.capsfilter, be.parse, be.tee, be.queue2, NULL);

#if 0
    ret = gst_element_link_filtered(be.src, be.convert, audio_caps);
    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] link error in bin", _FILE_, __LINE__);
        return ret;
    }
#endif
    //gst_element_link_many(be.convert, be.resample, NULL);
#if 0
    GstCaps *audioresample_caps; = gst_caps_new_simple("audio/x-raw", "rate", G_TYPE_INT, 48000, NULL);
    g_object_set(G_OBJECT(be.capsfilter), "caps", audioresample_caps, NULL);
    gst_caps_unref(audioresample_caps);
#endif
    if(!gst_element_link_filtered(be.src, be.convert, audio_caps) ||
        !gst_element_link_many(be.convert, be.queue, be.resample, be.capsfilter, be.rate, be.enc, be.queue2, be.tee, NULL))
        //!gst_element_link_many(be.convert, be.rate, be.queue, be.enc, be.tee, NULL))
        //!gst_element_link_many(be.convert, be.queue, be.rate, be.enc, be.tee, NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] link error", _FILE_, __LINE__);
        return -1;
    }

    ret =gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    //GstClock *alsasrc_clock = gst_element_provide_clock(be.src);
    //gst_pipeline_use_clock(GST_PIPELINE(pipeline), alsasrc_clock);
    //gst_element_set_clock(be.src, gst_system_clock_obtain());

    gst_caps_unref(audio_caps);

    return ret;
}