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

GstElement *pipeline = NULL;
GMainLoop *loop = NULL;
volatile sig_atomic_t is_interrupted = 0;
gboolean is_live = FALSE;
CmdArg cmdArg;

void log_once(gint opt, const gchar *message) 
{
    static gchar lastMessage[256][256];
    static guint8 ptr = 0;

    for(guint8 i=0; i<ptr; i++)
    {
        if (strcmp(message, lastMessage[i]) == 0) {
            return;
        }
    }

    if(opt <= cmdArg.log_level || opt <= LOG_ALERT)
    {
        syslog( opt|LOG_LOCAL0, "%s", message);
    }
    if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT)
    {
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");
        const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};

		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);
        g_print("%s\n", message);
        g_date_time_unref(datetime);
        g_free(date_str);
    }

    strncpy(lastMessage[ptr], message, sizeof(lastMessage));
    ptr++;
}

void mylog(gint opt, const gchar* _szfmt, ... )
{
	va_list va;
	gchar strTmp[512];

	va_start( va, _szfmt );
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= cmdArg.log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
        GDateTime *datetime = g_date_time_new_now_local();
        //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");

        //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
        //memcpy(file_name, tmp, 128);
        const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}

guint charArrayToInt(gchar *arr)
{
    guint8 i;
    guint result = 0;

    for(i=0; (arr[i]!='\n' && arr[i]!='\r' && arr[i]!='\0' && arr[i]!=' '); i++)
    {
        //g_print("arr[%d] : %c %d\n", i, arr[i], arr[i]);
        result = (result * 10) + (arr[i] - '0');
    }

    //g_print("result : %d\n", result);

    return result;
}

gboolean compareBuf(guint8 *cmp1, guint8 *cmp2, guint8 len)
{
	guint8 i;

	for(i=0;i<len;i++)
	{
		if(cmp1[i] != cmp2[i])
			return 0;
	}
	return 1;
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
    GOptionContext *ctx;
    GError *err = NULL;

    cmdArg.ohtName = cmdArg.appname;
    cmdArg.ch_enable = 0x0f;
    cmdArg.ioMode = AUTO_MODE;
    cmdArg.testMode = NORMAL_MODE;
    cmdArg.resMode = ResFHD;
    cmdArg.log_level = DEFAULT_LOG_LEVEL;
    cmdArg.dbg_level = DEFAULT_DBG_LEVEL;
    cmdArg.dotDir = DEFULAT_DOT_PATH;
    cmdArg.mntDir = DEFAULT_MOUNT_PATH;
    cmdArg.captureDir = DEFAULT_CAPTURE_PATH;
    cmdArg.play_delay = DEFAULT_PLAY_DELAY;
    cmdArg.fault = FALSE;
    cmdArg.duration = DEFUALT_DURATION;
    cmdArg.audio_en = FALSE;
    cmdArg.capture_en = FALSE;
    cmdArg.captureMaxCnt = DEFAULT_CAPTURE_MAX_CNT;
    cmdArg.input_en = FALSE;
    cmdArg.rtsp_port = DEFAULT_RTSP_PORT;
    cmdArg.rtsp_id = DEFAULT_RTSP_ID;
    cmdArg.rtsp_passwd = DEFAULT_RTSP_PASSWD;
    cmdArg.overlay_en = FALSE;
    cmdArg.split_margin_sec = DEFAULT_SPLIT_MARGIN_SEC;
    cmdArg.main_fps = DEFAULT_MAIN_FPS;
    cmdArg.fps[STREAM_REC] = DEFAULT_RECORD_FPS;
    cmdArg.fps[STREAM_RTSP] = DEFAULT_RTSP_FPS;
    cmdArg.bps[STREAM_REC] = DEFAULT_RECORD_BITRATE;
    cmdArg.bps[STREAM_RTSP] = DEFAULT_RTSP_BITRATE;
    cmdArg.stream_en[STREAM_REC] = TRUE;
    cmdArg.stream_en[STREAM_RTSP] = TRUE;
    cmdArg.gop[STREAM_REC] = DEFAULT_GOP_SIZE;
    cmdArg.gop[STREAM_RTSP] = DEFAULT_GOP_SIZE;
    cmdArg.csiRotationMode[CSI_1] = NONE_MODE;
    cmdArg.csiRotationMode[CSI_2] = NONE_MODE;
    for(guint8 i=0; i<MAX_CHANNEL; i++) {
        cmdArg.recRotationMode[i] = NONE_MODE;
        cmdArg.rtspRotationMode[i] = NONE_MODE;
    }

    GOptionEntry entries[] = {
        {"mode", 'm', 0, G_OPTION_ARG_INT, &cmdArg.ioMode, "io mode select 0(auto), 1(rw), 2(mmap), 3(userptr), 4(dmabuf), 5(dmabuf-import), default(0)", "INT"},
        {"test", 't', 0, G_OPTION_ARG_INT, &cmdArg.testMode, "test mode select 0(normal), 1(test), default(0)", "INT"},
        {"debug", 'd', 0, G_OPTION_ARG_INT, &cmdArg.dbg_level, "debug level, default(5)", "INT"},
        {"log", 'l', 0, G_OPTION_ARG_INT, &cmdArg.log_level, "log level, default(6)", "INT"},
        {"dot", 'T', 0, G_OPTION_ARG_STRING, &cmdArg.dotDir, "save dot representation of pipeline to FILE and exit, default(/tmp)", "STRING"},
        {"channel", 'n', 0, G_OPTION_ARG_INT, &cmdArg.ch_enable, "camera channel enable bit, default(0x0f)", "HEX"},
        {"output", 'O', 0, G_OPTION_ARG_STRING, &cmdArg.mntDir, "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
        {"res", 'e', 0, G_OPTION_ARG_INT, &cmdArg.resMode, "resolution select FHD(0) and HD(1), default(FHD)", "INT"},
        {"fmain", 'M', 0, G_OPTION_ARG_INT, &cmdArg.main_fps, "main frame per second, default(15)", "INT"},
        {"frec", 'f', 0, G_OPTION_ARG_INT, &cmdArg.fps[STREAM_REC], "record frame per second, default(15)", "INT"},
        {"frtsp", 'F', 0, G_OPTION_ARG_INT, &cmdArg.fps[STREAM_RTSP], "rtsp frame per second, default(15)", "INT"},
        {"brec", 'b', 0, G_OPTION_ARG_INT, &cmdArg.bps[STREAM_REC], "record Kbyte per second, default(4096)", "INT"},
        {"brtsp", 'B', 0, G_OPTION_ARG_INT, &cmdArg.bps[STREAM_RTSP], "rtsp Kbyte per second, default(1024)", "INT"},
        {"oht", 'o', 0, G_OPTION_ARG_STRING, &cmdArg.ohtName, "oht name, default(APPNAME)", "STRING"},
        {"delay", 'D', 0, G_OPTION_ARG_INT, &cmdArg.play_delay, "from pause to play delay, default(0)", "SECOND"},
        {"fault", NULL, 0, G_OPTION_ARG_NONE, &cmdArg.fault, "no fault setup, default(FALSE)", "NONE"},
        {"duration", 's', 0, G_OPTION_ARG_INT, &cmdArg.duration, "recoding file split duration, default(1)", "MINUTE"},
        {"erec", 'r', 0, G_OPTION_ARG_INT, &cmdArg.stream_en[STREAM_REC], "video recording enable, default(1)", "INT"},
        {"ertsp", 'R', 0, G_OPTION_ARG_INT, &cmdArg.stream_en[STREAM_RTSP], "rtsp streaming enable, default(1)", "INT"},
        {"eaudio", 'a', 0, G_OPTION_ARG_NONE, &cmdArg.audio_en, "audio recording enable, default(FALSE)", "NONE"},
        {"ecap", 'c', 0, G_OPTION_ARG_NONE, &cmdArg.capture_en, "video capturing enable, default(FALSE)", "NONE"},
        {"ein", 'i', 0, G_OPTION_ARG_NONE, &cmdArg.input_en, "terminal input enable, default(FALSE)", "NONE"},
        {"port", 'p', 0, G_OPTION_ARG_STRING, &cmdArg.rtsp_port, "rtsp port number, default(8554)", "STRING"},
        {"id", 'I', 0, G_OPTION_ARG_STRING, &cmdArg.rtsp_id, "rtsp id, default(semes)", "STRING"},
        {"passwd", 'P', 0, G_OPTION_ARG_STRING, &cmdArg.rtsp_passwd, "rtsp passwd, default(semes)", "STRING"},
        {"cmax", 'x', 0, G_OPTION_ARG_INT, &cmdArg.captureMaxCnt, "capture max count, default(3)", "INT"},
        {"eover", 'v', 0, G_OPTION_ARG_NONE, &cmdArg.overlay_en, "overlay enable, default(FALSE)", "NONE"},
        {"split", 'S', 0, G_OPTION_ARG_INT, &cmdArg.split_margin_sec, "split margin sec, default(3)", "INT"},
        {"grec", 'g', 0, G_OPTION_ARG_INT, &cmdArg.gop[STREAM_REC], "rec gop size, default(15)", "INT"},
        {"grtsp", 'G', 0, G_OPTION_ARG_INT, &cmdArg.gop[STREAM_RTSP], "rtsp gop size, default(15)", "INT"},
        {"csi0rt", NULL, 0, G_OPTION_ARG_INT, &cmdArg.csiRotationMode[CSI_1], "csi0 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"csi1rt", NULL, 0, G_OPTION_ARG_INT, &cmdArg.csiRotationMode[CSI_2], "csi1 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"rt0", NULL, 0, G_OPTION_ARG_INT, &cmdArg.recRotationMode[0], "rec ch0 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"rt1", NULL, 0, G_OPTION_ARG_INT, &cmdArg.recRotationMode[1], "rec ch1 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"rt2", NULL, 0, G_OPTION_ARG_INT, &cmdArg.recRotationMode[2], "rec ch2 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"rt3", NULL, 0, G_OPTION_ARG_INT, &cmdArg.recRotationMode[3], "rec ch3 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"RT0", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtspRotationMode[0], "rtsp ch0 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"RT1", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtspRotationMode[1], "rtsp ch1 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"RT2", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtspRotationMode[2], "rtsp ch2 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {"RT3", NULL, 0, G_OPTION_ARG_INT, &cmdArg.rtspRotationMode[3], "rtsp ch3 rotation mode select 0(NONE), 1(90), 2(180), 3(270), 4(horizontal), 5(vertical), default(0)", "INT"},
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    if(!g_option_context_parse(ctx, argc, argv, &err))
    {
        __LOG(LOG_ERR, "[GST][%s:%d] Failed to initialize: %s", _FILE_, __LINE__, err->message);
        g_error_free(err);
        return -1;
    }

    g_print("oht name : %s\n", cmdArg.ohtName);
    g_print("io mode : %d\n", cmdArg.ioMode);
    g_print("test mode : %s\n", cmdArg.testMode? "test":"normal");
    g_print("log_level : %d\n", cmdArg.log_level);
    g_print("debug_level : %d\n", cmdArg.dbg_level);
    g_print("mount directory : %s\n", cmdArg.mntDir);
    g_print("save dot directory : %s\n", cmdArg.dotDir);
    g_print("save capture directory : %s\n", cmdArg.captureDir);
    g_print("ch_enable : 0x%02x\n", cmdArg.ch_enable);
    g_print("record stream enable : %s\n", cmdArg.stream_en[STREAM_REC]? "TURE":"FALSE");
    g_print("rtsp stream enable : %s\n", cmdArg.stream_en[STREAM_RTSP]? "TURE":"FALSE");
    g_print("audio record enable : %s\n", cmdArg.audio_en? "TURE":"FALSE");
    g_print("captrue enable : %s\n", cmdArg.capture_en? "TURE":"FALSE");
    g_print("captrue max count : %d\n", cmdArg.captureMaxCnt);
    g_print("terminal input enable : %s\n", cmdArg.input_en? "TURE":"FALSE");
    g_print("res mode : %s\n", cmdArg.resMode? "HD":"FHD");
    g_print("width : %d\n", cmdArg.res[cmdArg.resMode].width);
    g_print("height : %d\n", cmdArg.res[cmdArg.resMode].height);
    g_print("main fps : %d\n", cmdArg.main_fps);
    g_print("rec fps : %d\n", cmdArg.fps[STREAM_REC]);
    g_print("rtsp fps : %d\n", cmdArg.fps[STREAM_RTSP]);
    g_print("rec bitrate : %d\n", cmdArg.bps[STREAM_REC]);
    g_print("rtsp bitrate : %d\n", cmdArg.bps[STREAM_RTSP]);
    g_print("rec gop size : %d\n", cmdArg.gop[STREAM_REC]);
    g_print("rtsp gop size : %d\n", cmdArg.gop[STREAM_RTSP]);
    g_print("play delay : %d\n", cmdArg.play_delay);
    g_print("no fault : %s\n", cmdArg.fault? "TURE":"FALSE");
    g_print("duration : %d\n", cmdArg.duration);
    g_print("rtsp port : %s\n", cmdArg.rtsp_port);
    g_print("rtsp id : %s\n", cmdArg.rtsp_id);
    g_print("rtsp passwd : %s\n", cmdArg.rtsp_passwd);
    g_print("overlay enable : %s\n", cmdArg.overlay_en? "TURE":"FALSE");
    g_print("split margin sec : %d\n", cmdArg.split_margin_sec);
    g_print("csi0 rotation : %d\n", cmdArg.csiRotationMode[CSI_1]);
    g_print("csi1 rotation : %d\n", cmdArg.csiRotationMode[CSI_2]);

    for(guint8 i=0; i<MAX_CHANNEL; i++){
        g_print("rec ch %d rotation : %d\n", i, cmdArg.recRotationMode[i]);
        g_print("rtsp ch %d rotation : %d\n", i, cmdArg.rtspRotationMode[i]);
    }

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

    static GstState state = GST_STATE_VOID_PENDING;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS) return TRUE;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_TAG) return TRUE;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT) return TRUE;

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
                //__LOG(LOG_INFO, "[GST][%s:%d] from %s to %s", _FILE_, __LINE__, gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
                state = old_state;
            }
            __LOG(LOG_INFO, "[GST][%s:%d] in %s", _FILE_, __LINE__, GST_OBJECT_NAME (message->src));
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
            gst_message_parse_qos (message,&live,&running_time,&stream_time,&timestamp,&duration);
            g_warning("GOt a QOS event %lu %lu %lu %lu", running_time, stream_time, timestamp, duration);
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %lu %f %d", jitter, prop, qual );

            GstFormat format;
            guint64 processed;
            guint64 dropped;
            gst_message_parse_qos_stats(message, &format, &processed, &dropped);

            g_print("QoS Message:\n");
            g_print("Format: %s\n", gst_format_get_name(format));
            g_print("Processed: %lu\n", processed);
            g_print("Dropped: %lu\n", dropped);
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

GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
    GstElement *data = (GstElement *)user_data;
    GstClockTime timestamp = GST_BUFFER_PTS(info->data);
    gchar *name;
    g_object_get(data, "name", &name, NULL);
    g_message("%s[%s]Timestamp: %" GST_TIME_FORMAT "\n", name, gst_pad_get_direction(pad)==1? "SRC":"SINK", GST_TIME_ARGS(timestamp));

    g_free(name);
    return GST_PAD_PROBE_OK;
}

gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    // 현재 시간 가져오기
    static GstClockTime prev_time = GST_CLOCK_TIME_NONE;
    GstClockTime current_time = GST_BUFFER_PTS(buffer);

    if (prev_time != GST_CLOCK_TIME_NONE) {
        // 이전 시간이 존재하면 현재 시간과의 차이 계산
        GstClockTimeDiff delay = current_time - prev_time;

        // 지연 출력
        g_print("Delay: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(delay));
    }

    // 이전 시간 업데이트
    prev_time = current_time;

    return TRUE;
}
