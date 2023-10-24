/*
 *
 * Cantops util.cpp support
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

guint8 log_level = 7;
guint8 dbg_level = 5;

GstElement *pipeline;
GMainLoop *loop;
volatile sig_atomic_t is_interrupted = 0;
gboolean is_live = FALSE;
CmdArg cmdArg;

void mylog( int opt, const char* _szfmt, ... )
{
	va_list va;
	char strTmp[512];
	const char *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};

	va_start( va, _szfmt );
	
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
        GDateTime *datetime = g_date_time_new_now_local();
        //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");

        //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
        //memcpy(file_name, tmp, 128);
 
		const char *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}


gint cmd_parser(int *argc, char **argv[], gpointer data)
{
#if 0
    gint debug_level = 0;
    gchar *saveDot = "/tmp";
    gchar *saveDir = "/mnt/sd_cam";
    gint ch_enable = 0x0f;
    gint resolution_mode = 0;
    gint rec_fps = RECORD_FPS;
    gint rtsp_fps = RTSP_FPS;
    gint rec_bitrate = RECORD_BITRATE;
    gint rtsp_bitrate = RTSP_BITRATE;
    gchar *ohtName = CHARNEXT(*argv[0], '/');
#endif
    //CmdArg* cmdArg = (CmdArg *)data;
    cmdArg.debug_level = 0;
    cmdArg.saveDot = "/tmp";
    cmdArg.saveDir = FILE_PATH;
    cmdArg.ch_enable = 0x0f;
    cmdArg.resMode = ResFHD;
    cmdArg.rec_fps = RECORD_FPS;
    cmdArg.rtsp_fps = RTSP_FPS;
    cmdArg.main_fps = MAIN_FPS;
    cmdArg.rec_bitrate = RECORD_BITRATE;
    cmdArg.rtsp_bitrate = RTSP_BITRATE;
    cmdArg.ohtName = CHARNEXT(*argv[0], '/');
    cmdArg.play_delay = 0;
    cmdArg.no_fault = TRUE;
    cmdArg.duration = DEFUALT_DURATION;
    GOptionContext *ctx;
    GError *err = NULL;

    cmdArg.res[0] = {1920, 1080};
    cmdArg.res[1] = {1280, 720};

    GOptionEntry entries[] = {
        {"debug", 'd', 0, G_OPTION_ARG_INT, &cmdArg.debug_level, "do not output status information, default(0)", "INT"},
        {"dot", 's', 0, G_OPTION_ARG_STRING, &cmdArg.saveDot, "save dot representation of pipeline to FILE and exit, default(/tmp)", "STRING"},
        {"channel", 'c', 0, G_OPTION_ARG_INT, &cmdArg.ch_enable, "camera channel enable bit, default(0x0f)", "HEX"},
        {"output", 'o', 0, G_OPTION_ARG_STRING, &cmdArg.saveDir, "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
        {"mode", 'm', 0, G_OPTION_ARG_INT, &cmdArg.resMode, "resolution select FHD(0) and HD(1), default(FHD)", "INT"},
        {"fmain", NULL, 0, G_OPTION_ARG_INT, &cmdArg.main_fps, "main frame per second, default(15)", "INT"},
        {"frec", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rec_fps, "record frame per second, default(15)", "INT"},
        {"frtsp", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtsp_fps, "rtsp frame per second, default(15)", "INT"},
        {"brec", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rec_bitrate, "record bit per second, default(4096)", "KBYTE"},
        {"brtsp", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtsp_bitrate, "rtsp bit per second, default(1024)", "KBYTE"},
        {"oht", 'O', 0, G_OPTION_ARG_STRING, &cmdArg.ohtName, "oht name, default(APPNAME)", "STRING"},
        {"delay", 'D', 0, G_OPTION_ARG_INT, &cmdArg.play_delay, "from pause to play delay, default(0)", "SECOND"},
        {"fault", 'f', 0, G_OPTION_ARG_NONE, &cmdArg.no_fault, "no fault setup, default(1)", "BOOLEAN"},
        {"duration", 't', 0, G_OPTION_ARG_INT, &cmdArg.duration, "file split duration, default(1)", "MINUTE"},
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    if(!g_option_context_parse(ctx, argc, argv, &err))
    {
        g_print("Failed to initialize: %s\n", err->message);
        g_error_free(err);
        return -1;
    }
    g_print("oht name : %s\n", cmdArg.ohtName);
    g_print("debug_level : %d\n", cmdArg.debug_level);
    g_print("saveDot : %s\n", cmdArg.saveDot);
    g_print("saveDirectory : %s\n", cmdArg.saveDir);
    g_print("ch_enable : 0x%02x\n", cmdArg.ch_enable);
    g_print("res mode : %s\n", cmdArg.resMode? "HD":"FHD");
    g_print("width : %d\n", cmdArg.res[cmdArg.resMode].width);
    g_print("height : %d\n", cmdArg.res[cmdArg.resMode].height);
    g_print("main fps : %d\n", cmdArg.main_fps);
    g_print("rec fps : %d\n", cmdArg.rec_fps);
    g_print("rtsp fps : %d\n", cmdArg.rtsp_fps);
    g_print("rec bitrate : %d\n", cmdArg.rec_bitrate);
    g_print("rtsp bitrate : %d\n", cmdArg.rtsp_bitrate);
    g_print("play delay : %d\n", cmdArg.play_delay);
    g_print("no fault : %s\n", cmdArg.no_fault? "TURE":"FALSE");
    g_print("duration : %d\n", cmdArg.duration);

    return 1;
}

static void print_tag(const GstTagList * list, const gchar * tag, gpointer unused)
{
    gint i, count;

    count = gst_tag_list_get_tag_size (list, tag);

    for (i = 0; i < count; i++) {
      gchar *str;

      if (gst_tag_get_type (tag) == G_TYPE_STRING) {
        if (!gst_tag_list_get_string_index (list, tag, i, &str))
          g_assert_not_reached ();
      } else {
        str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
      }

      if (i == 0) {
        g_print ("  %15s: %s\n", gst_tag_get_nick (tag), str);
      } else {
        g_print ("                 : %s\n", str);
      }

      g_free (str);
    }
}

gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    //PipeMain *info = (PipeMain *)data;
    static guint8 cam_cnt = 0;

    static GstState state = GST_STATE_PLAYING;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS) return TRUE;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_TAG) return TRUE;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_STREAM_STATUS) return TRUE;

    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    if(GST_MESSAGE_TYPE(message) != GST_MESSAGE_STATE_CHANGED)
        __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));

    switch(GST_MESSAGE_TYPE(message)) 
    {
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            if(state != old_state)
            {
                __LOG(LOG_INFO, "[GST][%s:%d] from %s to %s", _FILE_, __LINE__, gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
                state = old_state;
            }
            __LOG(LOG_DEBUG, "[GST][%s:%d] in %s", _FILE_, __LINE__, GST_OBJECT_NAME (message->src));
            break;
        }

        case GST_MESSAGE_ERROR:
        {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            __LOG(LOG_ERR, "[GST][%s:%d] error message : %s\n", __FILE__, __LINE__, err->message);
            __LOG(LOG_ERR, "[GST][%s:%d] error debug : %s\n", __FILE__, __LINE__, (debug)? debug : "none");
            printf("Error : %s\n", err->message);
            printf("Debug : %s\n", (debug)? debug : "none");
            g_error_free(err);
            g_free(debug);
            //destroy();
            gst_element_send_event(pipeline, gst_event_new_eos());
            break;
        }

        case GST_MESSAGE_EOS:
        {
            //printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
            //__LOG(LOG_NOTICE, "[GST][%s:%d] GST_MESSAGE_EOS (index:%d sigflag:%d)", _FILE_, __LINE__, info->index, sigflag);
            if(is_interrupted)
            {
                cam_cnt++;
                //if(cam_cnt >= MAX_PIPELINE) destroy();
            }
            else
            {
                gst_element_set_state(pipeline, GST_STATE_READY);
                //gst_element_get_state(pipeline[info->index], NULL, NULL, GST_CLOCK_TIME_NONE);
                //gst_element_seek(pipeline[info->index], 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);
                //gst_element_set_state(pipeline[info->index], GST_STATE_NULL);
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                //change_file_datetime(NULL);
            }

            break;
        }

        case GST_MESSAGE_ELEMENT:
            __LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));
            //g_print("%s\n", gst_structure_to_string(gst_message_get_structure(message)));
            break;

        case GST_MESSAGE_STREAM_STATUS:
        {
            GstStreamStatusType type;
            GstElement *owner;
            const GValue *val;
            gchar *path;
            GstTask *task = NULL;

            g_message ("received STREAM_STATUS");
            gst_message_parse_stream_status (message, &type, &owner);

            val = gst_message_get_stream_status_object (message);

            g_message ("type:   %d", type);
            path = gst_object_get_path_string (GST_MESSAGE_SRC (message));
            g_message ("source: %s", path);
            g_free (path);
            path = gst_object_get_path_string (GST_OBJECT (owner));
            g_message ("owner:  %s", path);
            g_free (path);
            g_message ("object: type %s, value %p", G_VALUE_TYPE_NAME (val),
            g_value_get_object (val));

            /* see if we know how to deal with this object */
            if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
                task = (GstTask *)g_value_get_object (val);
            }

            switch (type) {
                case GST_STREAM_STATUS_TYPE_CREATE:
                g_message ("created task %p", task);
                break;
                case GST_STREAM_STATUS_TYPE_ENTER:
                /* g_message ("raising task priority"); */
                /* setpriority (PRIO_PROCESS, 0, -10); */
                break;
                case GST_STREAM_STATUS_TYPE_LEAVE:
                break;
                default:
                break;
            }
            break;
        }

        case GST_MESSAGE_QOS:
        {
#if 1
            gboolean live;
            guint64 running_time, stream_time,timestamp,duration;
            // gst_message_parse_qos (msg,&live,&running_time,&stream_time,&timestamp,&duration);
            // g_warning("GOt a QOS event %llu %llu %llu %llu", running_time, stream_time, timestamp, duration);
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %lld %f %lu", jitter, prop, qual );

            GstFormat format;
            guint64 processed;
            guint64 dropped;
            gst_message_parse_qos_stats(message, &format, &processed, &dropped);

            g_print("QoS Message:\n");
            g_print("Format: %s\n", gst_format_get_name(format));
            g_print("Processed: %llu\n", processed);
            g_print("Dropped: %llu\n", dropped);
#endif
            break;
        }

        case GST_MESSAGE_TAG: 
        {
#if 1
            GstTagList *tags = NULL;
            gst_message_parse_tag (message, &tags);
            g_print ("Got tags from element %s\n", GST_OBJECT_NAME (message->src));
            gst_tag_list_foreach (tags, print_tag, NULL);
            gst_tag_list_free (tags);
            //handle_tags (tags);
            //gst_tag_list_unref (tags);
#endif
            break;
        }
#if 0
        case GST_MESSAGE_BUFFERING: 
        {
            gint percent = 0;

            /* If the stream is live, we do not care about buffering. */
            if (info->is_live) break;

            gst_message_parse_buffering (message, &percent);
            g_print ("Buffering (%3d%%)\r", percent);
            /* Wait until buffering is complete before start/resume playing */

            if (percent < 100)
                gst_element_set_state (pipeline, GST_STATE_PAUSED);
            else
                gst_element_set_state (pipeline, GST_STATE_PLAYING);
            break;

        }
#endif
        case GST_MESSAGE_CLOCK_LOST:
        {
            /* Get a new clock */
            g_print ("GST_MESSAGE_CLOCK_LOST\r");
            gst_element_set_state (pipeline, GST_STATE_PAUSED);
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
            break;
        }

        case GST_MESSAGE_LATENCY:
        {
                // when pipeline latency is changed, this msg is posted on the bus. we then have
                // to explicitly tell the pipeline to recalculate its latency
                // FIXME: this never works!
#if 1
                if (!gst_bin_recalculate_latency (GST_BIN(pipeline)))
                    g_print("Could not reconfigure latency.\n");
                else
                    g_print("Reconfigured latency.\n");
                break;
#endif

        }     
        case GST_MESSAGE_APPLICATION:{
            const GstStructure *s;
            s = gst_message_get_structure (message);
            if (gst_structure_has_name (s, "GstLaunchInterrupt")) {
            /* this application message is posted when we caught an interrupt and
            * we need to stop the pipeline. */
            g_print(("Interrupt: Stopping pipeline ...\n"));
            //res = ELR_INTERRUPT;
            //goto exit;
            }
            break;
        }

        default:
            break;

    }

    return TRUE;
}
