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
#include "testBin.h"
#include "muxSinkBin.h"
#include "rtspServerBin.h"
#include "captureBin.h"
#include <glib-unix.h>
#include <fcntl.h>

#define RECORDBIN_ENABLE
#define RTSPSERVERBIN_ENABLE
#define AUDIOBIN_ENABLE
#define MUXBIN_ENABLEx
//MuxSinkBin muxSinkBin[MAX_CHANNEL];

#define GST_API_VERSION "1.0"

typedef struct {
    void* arg0;
    void* arg1;
    void* arg2;
} ThreadArgs;

static gboolean quiet = FALSE;
extern volatile gboolean glib_on_error_halt;
guint signal_watch_intr_id;
guint signal_watch_hup_id;
gboolean ch_en_array[MAX_CHANNEL] = { TRUE, TRUE, TRUE, TRUE };

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

static void check_terminal_input(gpointer arg)  //(gpointer arg0, gpointer arg1, gpointer arg2) 
{
    ThreadArgs *thraedArgs = (ThreadArgs *)arg;
    RecordBin *recordBin = (RecordBin *)(thraedArgs->arg0);
    RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg1);
    CaptureBin *captrueBin = (CaptureBin *)(thraedArgs->arg2);
    //RecordBin *recordBin = (RecordBin *)arg0;
    //RtspServerBin *rtspServerBin = (RtspServerBin *)arg1;
    //CaptureBin *captrueBin = (CaptureBin *)arg2;
    int bytesRead;
    GstState state;
    guint8 i;
    gchar *cmd;
    gchar *token = NULL;
    guint bps, fps;
    char buffer[64];

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);

    do
    {
        g_usleep(1000);

        if(is_interrupted)
            break;

        bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (bytesRead == -1)
        {

        }
        else if(bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            g_print("Input: %s", buffer);

            do
            {
                token = strtok(buffer, " ");

                if (!strcmp(token, "get"))
                {
                    token = strtok(NULL, " ");
                    if(!strcmp(token, "bps"))
                    {
                        token = strtok(NULL, "\n");
                        if(!strcmp(token, "rec"))
                        {
                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (recordBin[i].getBinSinkPad()) recordBin[i].getBitrate();
                        }
                        if(!strcmp(token, "rtsp"))
                        {
                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].getBitrate();
                        }
                    }
                    else if(!strcmp(token, "fps"))
                    {
                        token = strtok(NULL, "\n");
                        if(!strcmp(token, "rec"))
                        {
                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (recordBin[i].getBinSinkPad()) recordBin[i].getFps();
                        }
                        if(!strcmp(token, "rtsp"))
                        {
                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].getFps();
                        }
                    }
                    else if(!strcmp(token, "cap"))
                    {
                        token = strtok(NULL, "\n");
                        if(!strcmp(token, "rec"))
                        {
                            //for (i = 0; i < MAX_CHANNEL; i++)
                                //if (recordBin[i].getBinSinkPad()) recordBin[i].getFps();
                        }
                        if(!strcmp(token, "rtsp"))
                        {
                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].getCaps();
                        }
                    }
                }
                if (!strcmp(token, "set"))
                {
                    token = strtok(NULL, " ");
                    if(!strcmp(token, "bps"))
                    {
                        token = strtok(NULL, " ");
                        if(!strcmp(token, "rec"))
                        {
                            token = strtok(NULL, "\0");
                            bps = charArrayToInt(token);

                            if(bps > 9999) {
                                __LOG(LOG_ERR, "[GST][%s:%d] bps %d not supported", _FILE_, __LINE__, bps);
                                break;
                            }

                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (recordBin[i].getBinSinkPad()) recordBin[i].setBitrate(bps);
                        }
                        if(!strcmp(token, "rtsp"))
                        {
                            token = strtok(NULL, "\0");
                            bps = charArrayToInt(token);

                            if(bps > 9999) {
                                __LOG(LOG_ERR, "[GST][%s:%d] bps %d not supported", _FILE_, __LINE__, bps);
                                break;
                            }

                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].setBitrate(bps);
                        }
                    }
                    else if(!strcmp(token, "fps"))
                    {
                        token = strtok(NULL, " ");
                        if(!strcmp(token, "rec"))
                        {
                            token = strtok(NULL, "\0");
                            fps = charArrayToInt(token);

                            if(fps > 99) {
                                __LOG(LOG_ERR, "[GST][%s:%d] fps %d not supported", _FILE_, __LINE__, bps);
                                break;
                            }

                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (recordBin[i].getBinSinkPad()) recordBin[i].setFps(fps);
                        }
                        if(!strcmp(token, "rtsp"))
                        {
                            token = strtok(NULL, "\0");
                            fps = charArrayToInt(token);

                            if(fps > 99) {
                                __LOG(LOG_ERR, "[GST][%s:%d] fps %d not supported", _FILE_, __LINE__, bps);
                                break;
                            }

                            for (i = 0; i < MAX_CHANNEL; i++)
                                if (rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].setFps(fps);
                        }
                    }
                }
                else if(!strncmp(buffer, "capture", 7))
                {
                    token = strtok(NULL, "\n");
                    if(!strcmp(token, "start"))
                    {
                        for (i = 0; i < MAX_CHANNEL; i++)
                            if (captrueBin[i].getBinSinkPad()) captrueBin[i].startCapture();
                    }
                    else if(!strcmp(token, "stop"))
                    {
                        for (i = 0; i < MAX_CHANNEL; i++)
                            if (captrueBin[i].getBinSinkPad()) captrueBin[i].stopCapture();
                    }
                }

            } while(0);
        }
    } while(1);

    __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s break", _FILE_, __LINE__, __FUNCTION__);

    return;
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

static void splitCheck(gpointer data, guint8 startSec)
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
            //if(!(cmdArg.ch_enable & (0x1 << i))) continue;
            if(!muxSinkBin[i].getBinVideoSinkPad()) continue;
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
        g_print("init target min : %d\n", target_min);
    }

    if(target_min != min)
    {
        g_date_time_unref(datetime);
        return;
    }

    if(sec == startSec+1)    //if(sec == 1 && microsec <= 50000)   //if(sec == 59 && microsec >= 700000 && microsec <b= 750000)
    {
        for(i=0; i<MAX_CHANNEL; i++) {
            if(muxSinkBin[i].getBinVideoSinkPad()) muxSinkBin[i].splitNow(NULL, FALSE);
        }

        //staticMin = min;
        target_min += cmdArg.duration;
        if(target_min >= 60) target_min -= 60;
        g_print("set target min : %d\n", target_min);
    }

    g_date_time_unref(datetime);

    return;
}

static void splitLoop(gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);
    while(1)
    {
        if(is_interrupted)
            break;

        splitCheck(data, 0);

        g_usleep(1000);
    }
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s break", _FILE_, __LINE__, __FUNCTION__);
}

static void taskLoop(gpointer arg)
{
    //splitCheck(data, 0);
    //splitTimerStart(data);
    //if(cmdArg.input_en) check_terminal_input(arg0, arg1, arg2);

    g_usleep(1000);
    
    return;
}

static gboolean setSRT(gpointer arg) 
{
    ThreadArgs *thraedArgs = (ThreadArgs *)arg;
    RecordBin *recordBin = (RecordBin *)(thraedArgs->arg0);
    RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg1);
    CaptureBin *captrueBin = (CaptureBin *)(thraedArgs->arg2);
    static gint index = 0;
    guint8 i;
    gchar* text;
    //GDateTime *datetime = g_date_time_new_now_local();
    //text = g_strdup_printf("2023-01-27 22:40:02 VD3001, M, A, 34049/174014(1000000), 1298.5678mm/s, 300mV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4");
#ifdef TIMEOVERLAY
    text = g_strdup_printf("VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4", index++);
#else
    text = g_strdup_printf("%s VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%%/71.5%%, E696, Level 7, Level 4", \
                        g_date_time_format(g_date_time_new_now_local(), "%Y-%m-%d %H:%M:%S"), index++);
#endif
    //__LOG(LOG_DEBUG, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(recordBin[i].getBinSinkPad()) recordBin[i].setOverlayText(text);
        if(rtspServerBin[i].getBinSinkPad()) rtspServerBin[i].setOverlayText(text);
    }

    index++;

    g_free(text);

    return TRUE;
}

gint main(gint argc, gchar *argv[]) 
{
    gst_init(&argc, &argv);
    atexit(cleanup);
    //attachInterruptHandlers();
    cmdArg.appname = CHARNEXT(argv[0], '/');
    cmd_parser(&argc, &argv, &cmdArg);

    //MuxBin* muxBin = MuxBin::getInstance();
    GstBus *bus;
    VideoBin videoBin[MAX_VIDEO_SRC];
    RecordBin recordBin[MAX_CHANNEL];
    RtspServerBin rtspServerBin[MAX_CHANNEL];
    MuxSinkBin muxSinkBin[MAX_CHANNEL];
    CaptureBin captureBin[MAX_CHANNEL];
    TestBin audioBin;
    ChannelNum chNum;
    GThread *splitThread = NULL, *terminalThread = NULL;
    ThreadArgs* thraedArgs = g_new(ThreadArgs, 1);
    GstStateChangeReturn ret;
    guint8 i;
    guint srtTimer_id = 0;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", __FILE__, __LINE__, PROGRAM_NAME);

    if(cmdArg.fault) fault_setup();

    g_setenv("GST_DEBUG_DUMP_DOT_DIR", cmdArg.dotDir, 1);
    pipeline = gst_pipeline_new(g_strdup_printf("pipeline-%s", cmdArg.appname));
    //muxBin.init();
    //g_print("width : %d\n", cmdArg.res[cmdArg.resolution_mode].height);

    signal_watch_intr_id = g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
    signal_watch_hup_id = g_unix_signal_add (SIGHUP, (GSourceFunc) hup_handler, pipeline);

    thraedArgs->arg0 = recordBin;
    thraedArgs->arg1 = rtspServerBin;
    thraedArgs->arg2 = captureBin;

#ifdef MUXBIN_ENABLE
    MuxBin muxBin;
    muxBin.init();
#endif

#if 0
    if(cmdArg.audio_en)
    {
        audioBin.init();
        audioBin.addElement("audiotestsrc", "audioconvert", "audiorate", "lamemp3enc", "mpegaudioparse", "queue", "tee", NULL);
        audioBin.linkElement();

        g_object_set(audioBin.be.element[0], "is-live", TRUE, NULL);
        //g_object_set(audioBin.be.element[0], "wave", 5, NULL);
        g_object_set(audioBin.be.element[0], "wave", 8, NULL);
        g_object_set(audioBin.be.element[0], "tick-interval", 200000000, NULL);
        //g_object_set(audioBin.be.element[2], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
        //g_object_set(audioBin.be.element[6], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
        //g_object_set(audioBin.be.element[6], "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", LEAKY_DOWNSTREAM, NULL);
    }
#endif

    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(!(cmdArg.ch_enable & (0x1 << i))) continue;
        chNum = (ChannelNum)i;
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch[%d] enable", _FILE_, __LINE__, chNum);
        videoBin[chNum/2].init((CsiNum)(chNum/2));
        //videoBin[chNum/2].addCrop((CropDir)(chNum%2));
        if(cmdArg.rec_en)
        {
            videoBin[chNum/2].addBinRecordSrcPad(chNum);
            recordBin[chNum].init(chNum);

            if(gst_pad_link(videoBin[chNum/2].getBinRecordSrcPad(chNum), recordBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] Record ch[%d] pad link err", _FILE_, __LINE__, chNum);
                //return -1;
                goto main_end;
            }
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);

            muxSinkBin[chNum].init(chNum);
            muxSinkBin[chNum].addBinVideoSinkPad();
            
            if(gst_pad_link(recordBin[chNum].getBinSrcPad(), muxSinkBin[chNum].getBinVideoSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] mux video ch[%d] pad link err", _FILE_, __LINE__, chNum);
                //return -1;
                goto main_end;
            }
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] mux video ch[%d] pad link", _FILE_, __LINE__, chNum);
            //g_thread_new("split-timer-thread", (GThreadFunc)splitTimerStart, &muxSinkBin[chNum]);
        }

        if(cmdArg.rtsp_en)
        {
            rtspStart();
            videoBin[chNum/2].addBinRtspSrcPad(chNum);
            rtspServerBin[chNum].init(chNum);

            if(gst_pad_link(videoBin[chNum/2].getBinRtspSrcPad(chNum), rtspServerBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] rtsp ch[%d] pad link err", _FILE_, __LINE__, chNum);
                //return -1;
                goto main_end;
            }
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);
        }

        if(cmdArg.audio_en)
        {
            if(!audioBin.init())
            {
                audioBin.addElement("audiotestsrc", "audioconvert", "audiorate", "lamemp3enc", "mpegaudioparse", "queue", "tee", NULL);
                if(!audioBin.linkElement()) {
                    //return -1;
                    goto main_end;
                }

                g_object_set(audioBin.be.element[0], "is-live", TRUE, NULL);
                //g_object_set(audioBin.be.element[0], "wave", 5, NULL);
                g_object_set(audioBin.be.element[0], "wave", 8, NULL);
                g_object_set(audioBin.be.element[0], "tick-interval", 200000000, NULL);
                //g_object_set(audioBin.be.element[2], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
                //g_object_set(audioBin.be.element[6], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
                //g_object_set(audioBin.be.element[6], "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", LEAKY_DOWNSTREAM, NULL);
            }

            audioBin.addBinSrcPad(chNum);
            muxSinkBin[chNum].addBinAudioSinkPad();
            if(gst_pad_link(audioBin.getBinSrcPad(chNum), muxSinkBin[chNum].getBinAudioSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] mux audio ch[%d] pad link err", _FILE_, __LINE__, chNum);
                //return -1;
                goto main_end;
            }
            else __LOG(LOG_NOTICE, "[GST][%s:%d] mux audio ch[%d] pad link", _FILE_, __LINE__, chNum);
        }

        if(cmdArg.capture_en)
        {
            videoBin[chNum/2].addBinCaptureSrcPad(chNum);
            captureBin[chNum].init(chNum);

            if(gst_pad_link(videoBin[chNum/2].getBinCaptureSrcPad(chNum), captureBin[chNum].getBinSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] capture ch[%d] pad link err", _FILE_, __LINE__, chNum);
                //return -1;
                goto main_end;
            }
        }

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
    }

    //gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(pipeline));

    bus = gst_element_get_bus(pipeline);
    if (!bus)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
        goto main_end;
    }

    gst_bus_add_watch(bus, my_bus_callback, NULL);

    gst_object_unref(bus);

    ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state paused error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        goto main_end;
    }
    else if(ret == GST_STATE_CHANGE_NO_PREROLL)
    {
        is_live = TRUE;
        __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state paused (ret : %d)", _FILE_, __LINE__, ret);
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] delay %d sec for play", __FILE__, __LINE__, cmdArg.play_delay);
    sleep(cmdArg.play_delay);

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state playing error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        goto main_end;
    }


    if(cmdArg.input_en) {
        terminalThread = g_thread_new("terminal-thread", (GThreadFunc)check_terminal_input, thraedArgs);
    }

    if(cmdArg.rec_en || cmdArg.audio_en) {
        splitThread = g_thread_new("split-thread", (GThreadFunc)splitLoop, muxSinkBin);
    }
    
    if(cmdArg.overlay_en) {
        srtTimer_id = g_timeout_add(500, (GSourceFunc)setSRT, thraedArgs);
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
            taskLoop(NULL);
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
        //if(rtspServerBin[i].getBinSinkPad()) gst_pad_send_event(rtspServerBin[i].getBinSinkPad(), gst_event_new_eos());
        //if(captureBin[i].getBinSinkPad()) gst_pad_send_event(captureBin[i].getBinSinkPad(), gst_event_new_eos());
    }
#endif

    sleep(1);

    if (gst_element_set_state (pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to unset pipeline state", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Pipeline unset successfully", _FILE_, __LINE__);
    }

main_end:
    rtspStop();
    if(loop) g_main_loop_unref(loop);
    if(pipeline) gst_object_unref(pipeline);
    if(thraedArgs) g_free(thraedArgs);

    if(terminalThread) {
        g_thread_join(terminalThread);
        g_thread_unref(terminalThread);
    }
    if(splitThread) {
        g_thread_join(splitThread);
        g_thread_unref(splitThread);
    }
    if(srtTimer_id) {
        g_source_remove(srtTimer_id);
    }

    if (signal_watch_intr_id > 0) g_source_remove(signal_watch_intr_id);
    if (signal_watch_hup_id > 0) g_source_remove(signal_watch_hup_id);

    return 0;
}