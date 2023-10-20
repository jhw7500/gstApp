/*
 *
 * Cantops main.cpp support
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

#include "global.h"
#include "util.h"
#include "videoBin.h"
#include "recordBin.h"
#include "muxBin.h"
#include "audioBin.h"
#include "muxSinkBin.h"
#include "rtspServerBin.h"

#define RECORDBIN_ENABLE
#define MUXBIN_ENABLEx
#define AUDIOBIN_ENABLEx
#define RTSPSERVERBIN_ENABLE

MuxSinkBin muxSinkBin[MAX_CHANNEL];

void sigHandler(int sig)
{
    guint8 i;
    static guint8 cnt = 0;
    __LOG(LOG_NOTICE, "[GST][%s:%d] sigHandler(%d)", _FILE_, __LINE__, sig);
	is_interrupted = 1;
    if(cnt++ > 3)
    {
        g_main_loop_unref(loop);
        gst_element_set_state (pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
    
    for(i=0; i<MAX_CHANNEL; i++)
    {
        gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(), gst_event_new_eos());
#ifdef AUDIOBIN_ENABLE
        gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(), gst_event_new_eos());
#endif
    }
}

void attachInterruptHandlers()
{
    //signal(SIGUSR1, ipcHandler);
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTERM, sigHandler);
}

void cleanup() {
    //g_message("Cleaning up GStreamer...");
    __LOG(LOG_CRIT, "[GST][%s:%d] Cleaning up GStreamer", _FILE_, __LINE__);
    gst_deinit();  // GStreamer 해제
}

static void splitTimerStart(gpointer data, gint startSec)
{
    MuxSinkBin* muxSinkBin = (MuxSinkBin *)data;
    static gboolean start_flag = 0;
    static gint staticMin = -1;

    if(start_flag == 0)
    {
        if(muxSinkBin[0].getStartFlag() == 0 && muxSinkBin[1].getStartFlag() == 0 && muxSinkBin[2].getStartFlag() == 0 && muxSinkBin[3].getStartFlag() == 0)
        {
            return;
        }
    }
    start_flag = 1;
    
    GDateTime *datetime = g_date_time_new_now_local();
    gint min = g_date_time_get_minute(datetime);
    gint sec = g_date_time_get_second(datetime);
    //gint microsec = g_date_time_get_microsecond(datetime);

    //g_print("sec:%d microsec:%d\n", sec, microsec);
    //__LOG(LOG_DEBUG, "[GST][%s:%d] sec:%d microsec:%d", _FILE_, __LINE__, sec, microsec);

    if(staticMin == min) 
    {
        g_date_time_unref(datetime);
        return;
    }

    if(sec == startSec+1)    //if(sec == 1 && microsec <= 50000)   //if(sec == 59 && microsec >= 700000 && microsec <b= 750000)
    {
        muxSinkBin[0].splitNow(NULL);
        muxSinkBin[1].splitNow(NULL);
        muxSinkBin[2].splitNow(NULL);
        muxSinkBin[3].splitNow(NULL);
        staticMin = min;
    }

    g_date_time_unref(datetime);

    return;
}

static void taskLoop(gpointer data)
{
    //MuxBin* muxBin = (MuxBin *)data;

    splitTimerStart(data, 0);
    g_usleep(1000);
    
    return;
}

int main(int argc, char *argv[]) 
{
    gst_init(&argc, &argv);
    atexit(cleanup);
    attachInterruptHandlers();
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", __FILE__, __LINE__, PROGRAM_NAME);
    VideoBin videoBin[MAX_VIDEO_SRC];
    RecordBin recordBin[MAX_CHANNEL];
    RtspServerBin rtspServerBin[MAX_CHANNEL];
    //GstElement *sink[MAX_CHANNEL];
    //MuxSinkBin muxSinkBin[MAX_CHANNEL];
    ChannelNum chNum;
    guint8 i;
    //MuxBin* muxBin = MuxBin::getInstance();

#if 1
    if(argc >= 1) {
        ohtName = CHARNEXT(argv[0], '/');
        //g_printf("%s\n", program_name);
        __LOG(LOG_NOTICE, "[GST][%s:%d] ohtName : %s", __FILE__, __LINE__, ohtName);
        //g_message("[GST][%s:%d] %s", __FILE__, __LINE__, program_name);
    }
#endif

    //ohtName = g_strdup_printf("%s", PRGORAM_NAME);
    cmd_parser(&argc, &argv);


    pipeline = gst_pipeline_new("pipeline");
    //muxBin.init();

#ifdef MUXBIN_ENABLE
    MuxBin muxBin;
    muxBin.init();
#endif

    

#ifdef RTSPSERVERBIN_ENABLE
        rtspStart();
#endif

#ifdef AUDIOBIN_ENABLE
    AudioBin audioBin;
    audioBin.init();
    audioBin.addElement("audiotestsrc", "audioconvert", "audiorate", "lamemp3enc", "mpegaudioparse", "queue", "tee", NULL);
    audioBin.linkElement();

    g_object_set(audioBin.ae.element[0], "is-live", TRUE, NULL);
    //g_object_set(audioBin.ae.element[0], "wave", 5, NULL);
    g_object_set(audioBin.ae.element[0], "wave", 8, NULL);
    g_object_set(audioBin.ae.element[0], "tick-interval", 200000000, NULL);
    g_object_set(audioBin.ae.element[2], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    g_object_set(audioBin.ae.element[6], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    //g_object_set(audioBin.ae.element[6], "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", LEAKY_DOWNSTREAM, NULL);
#endif

    for(i=0; i<MAX_CHANNEL; i++)
    {
        chNum = (ChannelNum)i;
        videoBin[chNum/2].init((CsiNum)(chNum/2));
        videoBin[chNum/2].addCrop((CropDir)(chNum%2));
#ifdef RECORDBIN_ENABLE
        videoBin[chNum/2].addBinRecordSrcPad(chNum);
        recordBin[chNum].init(chNum);

        if(gst_pad_link(videoBin[chNum/2].getBinRecordSrcPad(chNum), recordBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Record ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);

        muxSinkBin[chNum].init(chNum);
        muxSinkBin[chNum].addBinVideoSinkPad();
        
        if(gst_pad_link(recordBin[chNum].getBinSrcPad(), muxSinkBin[chNum].getBinVideoSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] mux video ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        else __LOG(LOG_NOTICE, "[GST][%s:%d] mux video ch[%d] pad link", _FILE_, __LINE__, chNum);
#endif

#ifdef AUDIOBIN_ENABLE
        audioBin.addBinSrcPad(chNum);
        muxSinkBin[chNum].addBinAudioSinkPad();
        if(gst_pad_link(audioBin.getBinSrcPad(chNum), muxSinkBin[chNum].getBinAudioSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] mux audio ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        else __LOG(LOG_NOTICE, "[GST][%s:%d] mux audio ch[%d] pad link", _FILE_, __LINE__, chNum);
#endif

#ifdef MUXBIN_ENABLE
        //muxBin.addSink(CH0);
        //muxBin.addPadVideoSink(CH0);
        muxBin.addSink(chNum);
        muxBin.addPadVideoSink(chNum);
        
        if(gst_pad_link(recordBin[chNum].getBinSrcPad(), muxBin.getBinVideoSinkPad(chNum)) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] mux ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }

        audioBin.addBinSrcPad(chNum);
        //muxBin.addPadAudioSink(CH0);
        muxBin.addPadAudioSink(chNum);

        if(gst_pad_link(audioBin.getBinSrcPad(chNum), muxBin.getBinAudioSinkPad(chNum)) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] audio ch[%d] pad link err", _FILE_, __LINE__, CH0);
            //return -1;
        }
#endif

#ifdef RTSPSERVERBIN_ENABLE
        //rtspStart();
        videoBin[chNum/2].addBinRtspSrcPad(chNum);
#if 1
        rtspServerBin[chNum].init(chNum);

        if(gst_pad_link(videoBin[chNum/2].getBinRtspSrcPad(chNum), rtspServerBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Record ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);
#endif

#endif
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/user/jhw/dot/", 1);
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(pipeline));

    GstBus *bus = gst_element_get_bus(pipeline);
    if (!bus)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
    }

    gst_bus_add_watch(bus, my_bus_callback, NULL);

    gst_object_unref(bus);

    loop = g_main_loop_new(NULL, FALSE);

	if(!loop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Main loop create error", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop start", _FILE_, __LINE__);
        //g_main_loop_run(gstLoop);
        while (!is_interrupted)
        {
            g_main_context_iteration(g_main_loop_get_context(loop), FALSE);
            //taskLoop(&muxBin);
            taskLoop(muxSinkBin);
        }
    }

    if (gst_element_set_state (pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to unset pipeline state", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Pipeline unset successfully", _FILE_, __LINE__);
    }
    rtspStop();
    g_main_loop_unref(loop);
    gst_object_unref(pipeline);

    return 0;
}