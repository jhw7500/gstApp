/*
 *
 * Cantops parser.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "parser.h"
#include "captureBin.h"
#include "videoBin.h"
#include "recordBin.h"
#include "rtspServerBin.h"
#include "muxSinkBin.h"
#include "aes.h"

static json_object *json_find_obj (json_object * jobj, char *find_key)
{
    size_t key_len = strlen(find_key);
    json_object_object_foreach(jobj, key, val) {
        if (strlen(key) == key_len && !memcmp (key, find_key, key_len)) return val;
    }
    return NULL;    // not found.
}

static gint json_object_get_value(json_object *hobj, const gchar *name, gpointer data)
{
    gint ret = 0;
    //json_object *vobj = json_find_obj(obj, (char *)name);
    json_object *vobj = json_object_object_get(hobj, name);
    enum json_type type = json_object_get_type(vobj);

    if(type == json_type_string) {
        gchar **val = (gchar**)data;
        *val = (gchar*)json_object_get_string(vobj);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_int) {
        gint *val = (gint *)data;
        *val = json_object_get_int(vobj);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] %s : %d", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_boolean) {
        gboolean *val = (gboolean *)data;
        *val = json_object_get_int(vobj);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name, *val? "TRUE":"FALSE");
    }
    else if(type == json_type_double) {
        gdouble *val = (gdouble *)data;
        *val = json_object_get_int(vobj);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] %s : %f", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_null)
    {
        __LOG(LOG_ERR, "[CFG][%s:%d] not exist : %s[%d]", _FILE_, __LINE__, name, type);
    }
    else
    {
        __LOG(LOG_ERR, "[CFG][%s:%d] unsupport type : %d", _FILE_, __LINE__, type);
    }

    //ret = json_object_put(vobj);

    return ret;
}

gint json_parser(const gchar *path)
{
	gint ret = 0;

	json_object *jobj = NULL;
    json_object *hobj = NULL;
	//const gchar* ptr;
	gchar* json_file;

    json_file = search_file(path, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
    __LOG(LOG_NOTICE, "[CFG][%s:%d] json file name : %s", _FILE_, __LINE__, json_file);

    if(strstr(json_file, JSON_NAME_PREFIX) == NULL || strstr(json_file, JSON_NAME_SUFFIX) == NULL) {
        __LOG(LOG_CRIT, "[CFG][%s:%d] json file name not match %s %s", _FILE_, __LINE__, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
        return -1;
    }

    jobj = json_object_from_file(json_file);
    enum json_type type = json_object_get_type(jobj);

	do {
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[CFG][%s:%d] data not json type[%d]", _FILE_, __LINE__, type);
			break;
		}

        //hobj = json_find_obj(jobj, "VHL_CAM");
        hobj = json_object_object_get(jobj, "VHL_CAM");
        type = json_object_get_type(hobj);
#if 1
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[CFG][%s:%d] data not json type[%d]", _FILE_, __LINE__, type);
			//break;
		}
#endif

#if 1
        json_object_get_value(hobj, "vhl_name", &cmdArg.ohtName);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] vhl_name : %s", _FILE_, __LINE__, cmdArg.ohtName);
        json_object_get_value(hobj, "id", &cmdArg.rtsp_id);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] id : %s", _FILE_, __LINE__, cmdArg.rtsp_id);
        //json_object_get_value(hobj, "passwd", &cmdArg.rtsp_passwd);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] rtsp_passwd : %s", _FILE_, __LINE__, cmdArg.rtsp_passwd);
        json_object_get_value(hobj, "cam_width", &cmdArg.width);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] cam_width : %d", _FILE_, __LINE__, cmdArg.width);
        json_object_get_value(hobj, "cam_height", &cmdArg.height);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] cam_height : %d", _FILE_, __LINE__, cmdArg.height);
        json_object_get_value(hobj, "recording_time", &cmdArg.duration);

        for(guint8 i=0; i<MAX_CHANNEL; i++)
        {
            json_object_get_value(hobj, g_strdup_printf("cam_ch%d", i), &cmdArg.cam_en[i]);
            json_object_get_value(hobj, g_strdup_printf("cam_ch%d_rotate", i), &cmdArg.cam_rotate[i]);
        }
#endif

    } while(0);

    //ret = json_object_put(jobj);
    //ret = json_object_put(hobj);

    return ret;
}

gint getPasswdWithAES(void)
{
	gint ret = 0;
	gchar passwd[1024] = { 0, };
    gchar *path;
	//info->encrypt.id = strdup(DEFAULT_ENCRYPT_ID);
	if(encrypt_get_passwd(path, passwd) < 0)
	{
		/* create */
		ret = encrypt_change_passwd(path, NULL, DEFAULT_RTSP_PASSWD);
		if(ret < 0) {
			__LOG(LOG_ERR, "[CFG][%s:%d] Error change passwd .. ", _FILE_, __LINE__);
		}
		cmdArg.rtsp_passwd = strdup(DEFAULT_RTSP_PASSWD);
	}
	else
		cmdArg.rtsp_passwd = strdup(passwd);

	__LOG(LOG_NOTICE, "[CFG][%s:%d] id : %s, passwd : %s", _FILE_, __LINE__, cmdArg.rtsp_id, cmdArg.rtsp_passwd);

	return ret;
}

gint arg_parser(int *argc, char **argv[], gpointer data)
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
    gchar str[128];
    gint ret = 0;

    GOptionEntry entries[] = {
        {"mode", 'M', 0, G_OPTION_ARG_INT, &cmdArg.ioMode, "io mode select 0(auto), 1(rw), 2(mmap), 3(userptr), 4(dmabuf), 5(dmabuf-import), default(0)", "INT"},
        {"test", 't', 0, G_OPTION_ARG_INT, &cmdArg.testMode, "test mode select 0(normal), 1(test), default(0)", "INT"},
        {"debug", 'd', 0, G_OPTION_ARG_INT, &cmdArg.dbg_level, "debug level, default(5)", "INT"},
        {"log", 'l', 0, G_OPTION_ARG_INT, &cmdArg.log_level, "log level, default(6)", "INT"},
        {"dot", 'T', 0, G_OPTION_ARG_STRING, &cmdArg.dotDir, "save dot representation of pipeline to FILE and exit, default(/tmp)", "STRING"},
        {"channel", 'a', 0, G_OPTION_ARG_INT, &cmdArg.ch_enable, "cam channel enable bit, default(0x0f)", "HEX"},
        {"rotation", 'A', 0, G_OPTION_ARG_INT, &cmdArg.ch_rotate, "cam channel rotation bit, default(0x00)", "HEX"},
        {"output", 'O', 0, G_OPTION_ARG_STRING, &cmdArg.mntDir, "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
        {"width", 'w', 0, G_OPTION_ARG_INT, &cmdArg.width, "cam width HD(1280), FHD(1920), default(1920)", "INT"},
        {"height", 'h', 0, G_OPTION_ARG_INT, &cmdArg.height, "cam height HD(720), FHD(1080), default(1080)", "INT"},
        {"fmain", 'm', 0, G_OPTION_ARG_INT, &cmdArg.main_fps, "main frame per second, default(15)", "INT"},
        {"frec", 'f', 0, G_OPTION_ARG_INT, &cmdArg.fps[STREAM_REC], "record frame per second, default(15)", "INT"},
        {"frtsp", 'F', 0, G_OPTION_ARG_INT, &cmdArg.fps[STREAM_RTSP], "rtsp frame per second, default(15)", "INT"},
        {"brec", 'b', 0, G_OPTION_ARG_INT, &cmdArg.bps[STREAM_REC], "record Kbyte per second, default(4096)", "INT"},
        {"brtsp", 'B', 0, G_OPTION_ARG_INT, &cmdArg.bps[STREAM_RTSP], "rtsp Kbyte per second, default(1024)", "INT"},
        {"oht", 'o', 0, G_OPTION_ARG_STRING, &cmdArg.ohtName, "oht name, default(APPNAME)", "STRING"},
        {"delay", 'D', 0, G_OPTION_ARG_INT, &cmdArg.play_delay, "from pause to play delay, default(0)", "SECOND"},
        {"fault", 0, 0, G_OPTION_ARG_NONE, &cmdArg.fault, "no fault setup, default(FALSE)", "NONE"},
        {"duration", 's', 0, G_OPTION_ARG_INT, &cmdArg.duration, "recoding file split duration, default(1)", "MINUTE"},
        {"erec", 'r', 0, G_OPTION_ARG_INT, &cmdArg.stream_en[STREAM_REC], "video recording enable, default(1)", "INT"},
        {"ertsp", 'R', 0, G_OPTION_ARG_INT, &cmdArg.stream_en[STREAM_RTSP], "rtsp streaming enable, default(1)", "INT"},
        {"eaudio", 'u', 0, G_OPTION_ARG_NONE, &cmdArg.audio_en, "audio recording enable, default(FALSE)", "NONE"},
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
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    if(!g_option_context_parse(ctx, argc, argv, &err))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to initialize : %s", _FILE_, __LINE__, err->message);
        g_error_free(err);
        return ret;
    }

    if(cmdArg.width != 1920 && cmdArg.width != 1280)
    {
        __LOG(LOG_ERR, "[GST][%s:%d] width %d not supported", _FILE_, __LINE__, cmdArg.width);
        return ret;
    }

    if(cmdArg.height != 1080 && cmdArg.height != 720)
    {
        __LOG(LOG_ERR, "[GST][%s:%d] height %d not supported", _FILE_, __LINE__, cmdArg.height);
        return ret;
    }

    //__LOG(LOG_CRIT, "[GST][%s:%d] cmdArg.ch_enable : 0x%02x", _FILE_, __LINE__, cmdArg.ch_enable);
    for(guint8 i=0; i<MAX_CHANNEL; i++)
    {
        if(cmdArg.ch_enable & (0x1 << i)) cmdArg.cam_en[i] = TRUE;
        else cmdArg.cam_en[i] = FALSE;
        if(cmdArg.ch_rotate & (0x1 << i)) cmdArg.cam_rotate[i] = TRUE;
        else cmdArg.cam_rotate[i] = FALSE;
        //__LOG(LOG_CRIT, "[GST][%s:%d] cmdArg.cam_en : %d", _FILE_, __LINE__, cmdArg.cam_en[i]);
    }

    sprintf(str, "echo %d > %s", (cmdArg.cam_rotate[1]<<1 | cmdArg.cam_rotate[0]) & 0x3, DEFAULT_ROTATE_PATH_01);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[DSK][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return ret;
    }

    sprintf(str, "echo %d > %s", (cmdArg.cam_rotate[3]<<1 | cmdArg.cam_rotate[2]) & 0x3, DEFAULT_ROTATE_PATH_23);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[DSK][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return ret;
    }

    ret = getPasswdWithAES();

    return ret;
}

void init_arg()
{
    cmdArg.ch_enable = DEFAULT_CH_ENABLE;
    cmdArg.ch_rotate = DEFAULT_CH_ROTATE;
    cmdArg.log_level = DEFAULT_LOG_LEVEL;
    cmdArg.dbg_level = DEFAULT_DBG_LEVEL;
    cmdArg.mntDir = DEFAULT_MOUNT_PATH;
    cmdArg.duration = DEFUALT_DURATION;
    cmdArg.rtsp_port = DEFAULT_RTSP_PORT;
    cmdArg.rtsp_id = DEFAULT_RTSP_ID;
    cmdArg.rtsp_passwd = DEFAULT_RTSP_PASSWD;
    cmdArg.main_fps = DEFAULT_MAIN_FPS;
    cmdArg.fps[STREAM_REC] = DEFAULT_RECORD_FPS;
    cmdArg.fps[STREAM_RTSP] = DEFAULT_RTSP_FPS;
    cmdArg.bps[STREAM_REC] = DEFAULT_RECORD_BITRATE;
    cmdArg.bps[STREAM_RTSP] = DEFAULT_RTSP_BITRATE;
    cmdArg.ohtName = cmdArg.appname;
    cmdArg.width = DEFAULT_WIDTH;
    cmdArg.height = DEFAULT_HEIGHT;

    cmdArg.ioMode = USERPTR_MODE;
    cmdArg.testMode = NORMAL_MODE;
    cmdArg.dotDir = DEFULAT_DOT_PATH;
    cmdArg.captureDir = DEFAULT_CAPTURE_PATH;
    cmdArg.play_delay = DEFAULT_PLAY_DELAY;
    cmdArg.fault = FALSE;
    cmdArg.audio_en = FALSE;
    cmdArg.capture_en = FALSE;
    cmdArg.captureMaxCnt = DEFAULT_CAPTURE_MAX_CNT;
    cmdArg.input_en = FALSE;
    cmdArg.overlay_en = FALSE;
    cmdArg.split_margin_sec = DEFAULT_SPLIT_MARGIN_SEC;
    cmdArg.stream_en[STREAM_REC] = TRUE;
    cmdArg.stream_en[STREAM_RTSP] = TRUE;
    cmdArg.gop[STREAM_REC] = DEFAULT_GOP_SIZE;
    cmdArg.gop[STREAM_RTSP] = DEFAULT_GOP_SIZE;

    for(guint8 i=0; i<MAX_CHANNEL; i++)
    {
        cmdArg.cam_en[i] = FALSE;
        cmdArg.cam_rotate[i] = FALSE;
    }
}

void print_option()
{
    g_print("oht name : %s\n", cmdArg.ohtName);
    g_print("io mode : %d\n", cmdArg.ioMode);
    g_print("test mode : %s\n", cmdArg.testMode? "test":"normal");
    g_print("log_level : %d\n", cmdArg.log_level);
    g_print("debug_level : %d\n", cmdArg.dbg_level);
    g_print("mount directory : %s\n", cmdArg.mntDir);
    g_print("save dot directory : %s\n", cmdArg.dotDir);
    g_print("save capture directory : %s\n", cmdArg.captureDir);
    //g_print("ch_enable : 0x%02x\n", cmdArg.ch_enable);
    g_print("record stream enable : %s\n", cmdArg.stream_en[STREAM_REC]? "TURE":"FALSE");
    g_print("rtsp stream enable : %s\n", cmdArg.stream_en[STREAM_RTSP]? "TURE":"FALSE");
    g_print("audio record enable : %s\n", cmdArg.audio_en? "TURE":"FALSE");
    g_print("captrue enable : %s\n", cmdArg.capture_en? "TURE":"FALSE");
    g_print("captrue max count : %d\n", cmdArg.captureMaxCnt);
    g_print("terminal input enable : %s\n", cmdArg.input_en? "TURE":"FALSE");
    //g_print("res mode : %s\n", cmdArg.resMode? "HD":"FHD");
    //g_print("width : %d\n", cmdArg.res[cmdArg.resMode].width);
    //g_print("height : %d\n", cmdArg.res[cmdArg.resMode].height);
    g_print("width : %d\n", cmdArg.width);
    g_print("height : %d\n", cmdArg.height);
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
    for(guint8 i=0; i<MAX_CHANNEL; i++){
        //g_print("rec ch %d rotation : %d\n", i, cmdArg.recRotationMode[i]);
        //g_print("rtsp ch %d rotation : %d\n", i, cmdArg.rtspRotationMode[i]);
        g_print("cam_en ch %d : %s\n", i, cmdArg.cam_en[i]? "TRUE":"FALSE");
        g_print("cam_rotate ch %d : %s\n", i, cmdArg.cam_rotate[i]? "TRUE":"FALSE");
    }
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] 0 : %s, 1 : %s, 2 : %s", _FILE_, __LINE__, test0, test1, test2);

    return;
}

gint cmd_parser(gchar* buffer, gpointer data)
{
    guint8 i;
    gchar *token = NULL;
    const gchar *stateStr[] = {"PENDING", "NULL", "READY", "PAUSED", "PLAYING"};
    ThreadArgs *thraedArgs = (ThreadArgs *)data;
    VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
    RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
    RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
    MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
    CaptureBin *captrueBin = (CaptureBin *)(thraedArgs->arg4);

    token = strtok(buffer, " ");

    if (!strcmp(token, "get"))
    {
        token = strtok(NULL, " ");
        if (!strcmp(token, "bps"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].getBitrate();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getBitrate();
            }
        }
        else if (!strcmp(token, "fps"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].getFps();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getFps();
            }
        }
        else if (!strcmp(token, "cap"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                // for (i = 0; i < MAX_CHANNEL; i++)
                // if (recordBin[i].getBinSinkPad()) recordBin[i].getFps();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getCaps();
            }
        }
        else if (!strcmp(token, "rotation"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].getRotation();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getRotation();
            }
        }
        else if (!strcmp(token, "state"))
        {
            token = strtok(NULL, "\n");
            GstState state;
            if (!strcmp(token, "rec"))
            {
                gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                __LOG(LOG_NOTICE, "[GST][%s:%d] rec state : %s[%d]", _FILE_, __LINE__, stateStr[state], state);
            }
            else if (!strcmp(token, "rtsp"))
            {
                gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                __LOG(LOG_NOTICE, "[GST][%s:%d] rtsp state : %s[%d]", _FILE_, __LINE__, stateStr[state], state);
            }
        }
        else if (!strcmp(token, "iomode"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "0"))
            {
                videoBin[0].getIoMode();
            }
            else if (!strcmp(token, "1"))
            {
                videoBin[1].getIoMode();
            }
        }
        else if (!strcmp(token, "gop"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].getGop();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getGop();
            }
        }
        else if (!strcmp(token, "key"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rec"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].getKeyframe();
            }
            else if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].getKeyframe();
            }
        }
    } // get
    else if (!strcmp(token, "set"))
    {
        token = strtok(NULL, " ");
        if (!strcmp(token, "bps"))
        {
            guint bps;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                bps = charArrayToInt(token);

                if (bps > 9999)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] bps %d not supported", _FILE_, __LINE__, bps);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].setBitrate(bps);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                bps = charArrayToInt(token);

                if (bps > 9999)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] bps %d not supported", _FILE_, __LINE__, bps);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setBitrate(bps);
            }
        }
        else if (!strcmp(token, "fps"))
        {
            guint fps;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                fps = charArrayToInt(token);

                if (fps > 99)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] fps %d not supported", _FILE_, __LINE__, fps);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].setFps(fps);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                fps = charArrayToInt(token);

                if (fps > 99)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] fps %d not supported", _FILE_, __LINE__, fps);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setFps(fps);
            }
        }
        else if (!strcmp(token, "rotation"))
        {
            guint rotation;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                rotation = charArrayToInt(token);

                if (rotation > 5)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] rotation %d not supported", _FILE_, __LINE__, rotation);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].setRotation(rotation);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                rotation = charArrayToInt(token);

                if (rotation > 5)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] rotation %d not supported", _FILE_, __LINE__, rotation);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setRotation(rotation);
            }
        }
        else if (!strcmp(token, "state"))
        {
            GstState state;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                state = (GstState)charArrayToInt(token);

                if (state > 4)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] state %d not supported", _FILE_, __LINE__, state);
                    return -1;
                }
                gst_element_set_state(pipeline, state);
                __LOG(LOG_NOTICE, "[GST][%s:%d] rec state : %s[%d]", _FILE_, __LINE__, stateStr[state], state);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                state = (GstState)charArrayToInt(token);

                if (state > 4)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] state %d not supported", _FILE_, __LINE__, state);
                    return -1;
                }
                gst_element_set_state(pipeline, state);
                __LOG(LOG_NOTICE, "[GST][%s:%d] rtsp state : %s[%d]", _FILE_, __LINE__, stateStr[state], state);
            }
        }
        else if (!strcmp(token, "iomode"))
        {
            guint ioMode;
            token = strtok(NULL, " ");
            if (!strcmp(token, "0"))
            {
                token = strtok(NULL, "\0");
                ioMode = charArrayToInt(token);

                if (ioMode > 5)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] ioMode %d not supported", _FILE_, __LINE__, ioMode);
                    return -1;
                }

                videoBin[0].setIoMode(ioMode);
            }
            else if (!strcmp(token, "1"))
            {
                token = strtok(NULL, "\0");
                ioMode = charArrayToInt(token);

                if (ioMode > 5)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] ioMode %d not supported", _FILE_, __LINE__, ioMode);
                    return -1;
                }

                videoBin[1].setIoMode(ioMode);
            }
        }
        else if (!strcmp(token, "gop"))
        {
            guint gop;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                gop = charArrayToInt(token);

                if (gop > 100)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] gop %d not supported", _FILE_, __LINE__, gop);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].setGop(gop);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                gop = charArrayToInt(token);

                if (gop > 100)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] gop %d not supported", _FILE_, __LINE__, gop);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setGop(gop);
            }
        }
        else if (!strcmp(token, "key"))
        {
            guint key;
            token = strtok(NULL, " ");
            if (!strcmp(token, "rec"))
            {
                token = strtok(NULL, "\0");
                key = charArrayToInt(token);

                if (key > 1)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] key %d not supported", _FILE_, __LINE__, key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (recordBin[i].getBinSinkPad())
                        recordBin[i].setkeyframe(key);
            }
            else if (!strcmp(token, "rtsp"))
            {
                token = strtok(NULL, "\0");
                key = charArrayToInt(token);

                if (key > 9999)
                {
                    __LOG(LOG_ERR, "[GST][%s:%d] key %d not supported", _FILE_, __LINE__, key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setkeyframe(key);
            }
        }
        else if (!strcmp(token, "dbg"))
        {
            token = strtok(NULL, "\n");
            if (!strcmp(token, "rtsp"))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (rtspServerBin[i].getBinSinkPad())
                        rtspServerBin[i].setTimeStampDebug();
            }
            else if (!strcmp(token, "rec"))
            {
            }
        }
    } // set
    else if (!strncmp(buffer, "capture", 7))
    {
        token = strtok(NULL, "\n");
        if (!strcmp(token, "start"))
        {
            for (i = 0; i < MAX_CHANNEL; i++)
                if (captrueBin[i].getBinSinkPad())
                    captrueBin[i].startCapture();
        }
        else if (!strcmp(token, "stop"))
        {
            for (i = 0; i < MAX_CHANNEL; i++)
                if (captrueBin[i].getBinSinkPad())
                    captrueBin[i].stopCapture();
        }
    }
    else if (!strncmp(buffer, "split", 5))
    {
        token = strtok(NULL, " ");
        if (!strcmp(token, "start"))
        {
            for (i = 0; i < MAX_CHANNEL; i++)
                if (muxSinkBin[i].getBinVideoSinkPad())
                    muxSinkBin[i].splitNow(NULL, FALSE);
        }
        else if (!strcmp(token, "set"))
        {
            token = strtok(NULL, "\0");
            gint set_sec = charArrayToInt(token);

            if (set_sec > 59)
            {
                __LOG(LOG_ERR, "[GST][%s:%d] set_sec %d not supported", _FILE_, __LINE__, set_sec);
                return -1;
            }
            __LOG(LOG_NOTICE, "[GST][%s:%d] split set_sec : %d", _FILE_, __LINE__, set_sec);

            GDateTime *datetime;
            gint sec;

            while (1)
            {
                datetime = g_date_time_new_now_local();
                sec = g_date_time_get_second(datetime);
                if (set_sec == sec)
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] set_sec %d = sec %d", _FILE_, __LINE__, set_sec, sec);
                    break;
                }

                usleep(1000);
            }

            for (i = 0; i < MAX_CHANNEL; i++)
                if (muxSinkBin[i].getBinVideoSinkPad())
                    muxSinkBin[i].splitNow(NULL, FALSE);

            g_date_time_unref(datetime);
        }
    }
    else
    {
        g_print("wrong cmd!\n");
    }

    return 0;
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

gboolean bus_message_parse(GstBus *bus, GstMessage *message, gpointer data)
{
    //PipeMain *info = (PipeMain *)data;
    static guint8 cam_cnt = 0;
    static GstState state = GST_STATE_VOID_PENDING;
    GstMessageType mType = GST_MESSAGE_TYPE(message);

    if(mType == GST_MESSAGE_QOS) return TRUE;

    if(mType == GST_MESSAGE_TAG) return TRUE;

    //if(mType == GST_MESSAGE_ELEMENT) return TRUE;

    if(mType == GST_MESSAGE_STREAM_STATUS) return TRUE;
    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    if(mType == GST_MESSAGE_STATE_CHANGED) return TRUE;

    //__LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));

    switch(mType) 
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
        {
            //__LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));
            //g_print("%s\n", gst_structure_to_string(gst_message_get_structure(message)));
 			const GstStructure *structure = gst_message_get_structure(message);
			if(gst_structure_has_name(structure, "splitmuxsink-fragment-opened"))
            {
                __LOG(LOG_NOTICE, "[GST][%s:%d] filename : %s, time : %llu", __FILE__, __LINE__, \
                    g_value_get_string(gst_structure_get_value(structure, "location")), g_value_get_uint64(gst_structure_get_value(structure, "running-time")));
            }
            else
                __LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));

            break;
        }

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
            __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));
            break;

    }

    return TRUE;
}