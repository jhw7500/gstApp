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

#include "util.h"
#include "videoBin.h"
#include "recordBin.h"
#include "muxBin.h"
#include "audioBin.h"
#include "muxSinkBin.h"
#include "rtspServerBin.h"
#include <glib-unix.h>

#define RECORDBIN_ENABLE
#define RTSPSERVERBIN_ENABLEx
#define AUDIOBIN_ENABLEx
#define MUXBIN_ENABLEx
//MuxSinkBin muxSinkBin[MAX_CHANNEL];

#define GST_API_VERSION "1.0"

static gboolean quiet = FALSE;
extern volatile gboolean glib_on_error_halt;
guint signal_watch_intr_id;
guint signal_watch_hup_id;

static void fault_restore (void);
static void fault_spin (void);

static gboolean intr_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;
  g_print("handling interrupt.\n");
  /* post an application specific message */
  gst_element_post_message (GST_ELEMENT (pipeline),
      gst_message_new_application (GST_OBJECT (pipeline),
          gst_structure_new ("GstLaunchInterrupt",
              "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));
  /* remove signal handler */
  signal_watch_intr_id = 0;
  is_interrupted = 1;
  return G_SOURCE_REMOVE;
}

static gboolean hup_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;
  if (g_getenv ("GST_DEBUG_DUMP_DOT_DIR") != NULL) {
    g_print("SIGHUP: dumping dot file snapshot ...\n");
  } else {
    g_print("SIGHUP: not dumping dot file snapshot, GST_DEBUG_DUMP_DOT_DIR "
        "environment variable not set.\n");
  }
  /* dump graph on hup */
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.snapshot");
  return G_SOURCE_CONTINUE;
}

static void fault_handler_sighandler (int signum)
{
  fault_restore ();
  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      fprintf (stderr, "Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      if (!quiet)
        printf ("Caught SIGQUIT\n");
      break;
    default:
      fprintf (stderr, "signo:  %d\n", signum);
      break;
  }
  fault_spin ();
}
static void fault_spin (void)
{
  int spinning = TRUE;
  glib_on_error_halt = FALSE;
  g_on_error_stack_trace ("/home/user/gstApp" GST_API_VERSION);
  wait (NULL);
  /* FIXME how do we know if we were run by libtool? */
  fprintf (stderr,
      "Spinning.  Please run 'gdb gstApp " GST_API_VERSION " %d' to "
      "continue debugging, Ctrl-C to quit, or Ctrl-\\ to dump core.\n",
      (gint) getpid ());
  while (spinning)
    g_usleep (1000000);
}
static void fault_restore (void)
{
  struct sigaction action;
  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;
  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}
static void fault_setup (void)
{
  struct sigaction action;
  memset (&action, 0, sizeof (action));
  action.sa_handler = fault_handler_sighandler;
  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

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

#if 0
    for(i=0; i<MAX_CHANNEL; i++)
    {
        gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(), gst_event_new_eos());
#ifdef AUDIOBIN_ENABLE
        gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(), gst_event_new_eos());
#endif
    }
#endif
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
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    gst_deinit();  // GStreamer 해제
}

static void splitTimerStart(gpointer data)
{
    MuxSinkBin* muxSinkBin = (MuxSinkBin *)data;
    GDateTime *datetime;
    gint sec;
    static gboolean start_f = FALSE;
    guint8 i;

    if(start_f) return;

    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(!(cmdArg.ch_enable & (0x1 << i))) continue;
        if (muxSinkBin[i].getStartFlag() == 0) return; 
    }

    datetime = g_date_time_new_now_local();
    sec = g_date_time_get_second(datetime);
    if (sec == 0)
    {
        for(i=0; i<MAX_CHANNEL; i++) muxSinkBin[i].splitNow(NULL, TRUE);
        start_f = TRUE;
    }

    g_date_time_unref(datetime);

    return;
}

static void splitCheck(gpointer data, gint startSec)
{
    MuxSinkBin* muxSinkBin = (MuxSinkBin *)data;
    static gboolean start_flag = 0;
    //static gint staticMin = -1;
    static gint target_min = -1;
    guint8 i;

    if(start_flag == 0)
    {
        //if(muxSinkBin[0].getStartFlag() == 0 && muxSinkBin[1].getStartFlag() == 0 && muxSinkBin[2].getStartFlag() == 0 && muxSinkBin[3].getStartFlag() == 0) return;
        for(i=0; i<MAX_CHANNEL; i++)
        {
            if(!(cmdArg.ch_enable & (0x1 << i))) continue;
            if (muxSinkBin[i].getStartFlag() == 0) return; 
        }
    }
    start_flag = 1;
    
    GDateTime *datetime = g_date_time_new_now_local();
    gint min = g_date_time_get_minute(datetime);
    gint sec = g_date_time_get_second(datetime);
    //gint microsec = g_date_time_get_microsecond(datetime);

    //g_print("sec:%d microsec:%d\n", sec, microsec);
    //__LOG(LOG_DEBUG, "[GST][%s:%d] sec:%d microsec:%d", _FILE_, __LINE__, sec, microsec);

    if(target_min == -1)
    {
        target_min = min + 1;
        if(target_min >= 60) target_min = 0;
        g_print("target min init : %d\n", target_min);
    }

    if(target_min != min)
        return;

    if(sec == startSec+1)    //if(sec == 1 && microsec <= 50000)   //if(sec == 59 && microsec >= 700000 && microsec <b= 750000)
    {
        for(i=0; i<MAX_CHANNEL; i++) muxSinkBin[i].splitNow(NULL, FALSE);

        //staticMin = min;
        target_min += cmdArg.duration;
        if(target_min >= 60) target_min -= 60;
        g_print("target min set : %d\n", target_min);
    }

    g_date_time_unref(datetime);

    return;
}

static void taskLoop(gpointer data)
{
    splitCheck(data, 0);
    //splitTimerStart(data);

    g_usleep(1000);
    
    return;
}

int main(int argc, char *argv[]) 
{
    //gst_init(&argc, &argv);
    atexit(cleanup);
    //attachInterruptHandlers();

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", __FILE__, __LINE__, PROGRAM_NAME);
    VideoBin videoBin[MAX_VIDEO_SRC];
    RecordBin recordBin[MAX_CHANNEL];
    RtspServerBin rtspServerBin[MAX_CHANNEL];
    //GstElement *sink[MAX_CHANNEL];
    MuxSinkBin muxSinkBin[MAX_CHANNEL];
    ChannelNum chNum;
    guint8 i;
    
    //MuxBin* muxBin = MuxBin::getInstance();

    //ohtName = g_strdup_printf("%s", PRGORAM_NAME);
    cmd_parser(&argc, &argv, &cmdArg);

    if(!cmdArg.no_fault) fault_setup();

    pipeline = gst_pipeline_new("pipeline");
    //muxBin.init();
    //g_print("width : %d\n", cmdArg.res[cmdArg.resolution_mode].height);

    signal_watch_intr_id = g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
    signal_watch_hup_id = g_unix_signal_add (SIGHUP, (GSourceFunc) hup_handler, pipeline);

#ifdef MUXBIN_ENABLE
    MuxBin muxBin;
    muxBin.init();
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
        if(!(cmdArg.ch_enable & (0x1 << i))) continue;
        chNum = (ChannelNum)i;
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch[%d] enable", _FILE_, __LINE__, chNum);
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
        //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);

        muxSinkBin[chNum].init(chNum);
        muxSinkBin[chNum].addBinVideoSinkPad();
        
        if(gst_pad_link(recordBin[chNum].getBinSrcPad(), muxSinkBin[chNum].getBinVideoSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] mux video ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        //else __LOG(LOG_NOTICE, "[GST][%s:%d] mux video ch[%d] pad link", _FILE_, __LINE__, chNum);
        //g_thread_new("split-timer-thread", (GThreadFunc)splitTimerStart, &muxSinkBin[chNum]);
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
        rtspStart();
        videoBin[chNum/2].addBinRtspSrcPad(chNum);
#if 1
        rtspServerBin[chNum].init(chNum);

        if(gst_pad_link(videoBin[chNum/2].getBinRtspSrcPad(chNum), rtspServerBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Record ch[%d] pad link err", _FILE_, __LINE__, chNum);
            //return -1;
        }
        //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);
#endif

#endif
    }

    //gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/user/jhw/dot/", 1);
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(pipeline));

    GstBus *bus = gst_element_get_bus(pipeline);
    if (!bus)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
    }

    gst_bus_add_watch(bus, my_bus_callback, NULL);

    gst_object_unref(bus);

    GstStateChangeReturn ret;
    
    ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state paused error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        return -1;
    }
    else if(ret == GST_STATE_CHANGE_NO_PREROLL)
    {
        is_live = TRUE;
        __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state paused (ret : %d)", _FILE_, __LINE__, ret);
    }

    sleep(cmdArg.play_delay);

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state playing error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        return -1;
    }

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
    __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop exit", _FILE_, __LINE__);

#if 0
    if(!gst_element_send_event(pipeline, gst_event_new_eos()))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to gst_element_send_event", _FILE_, __LINE__);
    }
#else
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(muxSinkBin[i].getBinVideoSinkPad()) gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(), gst_event_new_eos());
        if(muxSinkBin[i].getBinAudioSinkPad()) gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(), gst_event_new_eos());
    }
#endif

    sleep(1);

    if (gst_element_set_state (pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to unset pipeline state", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Pipeline unset successfully", _FILE_, __LINE__);
    }

    rtspStop();
    g_main_loop_unref(loop);
    gst_object_unref(pipeline);

    if (signal_watch_intr_id > 0)
      g_source_remove (signal_watch_intr_id);
    if (signal_watch_hup_id > 0)
      g_source_remove (signal_watch_hup_id);

    return 0;
}