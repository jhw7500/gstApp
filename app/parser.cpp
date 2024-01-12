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

json_object *ParserClass::json_find_obj (json_object * jobj, char *find_key)
{
    size_t key_len = strlen(find_key);
    json_object_object_foreach(jobj, key, val) {
        if (strlen(key) == key_len && !memcmp (key, find_key, key_len)) return val;
    }
    return NULL;    // not found.
}

gint ParserClass::json_object_get_value(json_object *hobj, const gchar *name, gpointer data)
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

ParserClass::ParserClass()
{
    // 생성자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

ParserClass::~ParserClass()
{
    // 소멸자 코드 추가
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

ParserClass* ParserClass::getInstance()
{
	static ParserClass instance;
	return &instance;
}

gint ParserClass::json_parser(const gchar *path)
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
        json_object_get_value(hobj, "vhl_name", &arg.ohtName);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] vhl_name : %s", _FILE_, __LINE__, arg.ohtName);
        json_object_get_value(hobj, "id", &arg.rtsp_id);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] id : %s", _FILE_, __LINE__, arg.rtsp_id);
        //json_object_get_value(hobj, "passwd", &arg.rtsp_passwd);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] rtsp_passwd : %s", _FILE_, __LINE__, arg.rtsp_passwd);
        json_object_get_value(hobj, "cam_width", &arg.width);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] cam_width : %d", _FILE_, __LINE__, arg.width);
        json_object_get_value(hobj, "cam_height", &arg.height);
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] cam_height : %d", _FILE_, __LINE__, arg.height);
        json_object_get_value(hobj, "recording_time", &arg.duration);

        for(guint8 i=0; i<MAX_CHANNEL; i++)
        {
            json_object_get_value(hobj, g_strdup_printf("cam_ch%d", i), &arg.cam_en[i]);
            json_object_get_value(hobj, g_strdup_printf("cam_ch%d_rotate", i), &arg.cam_rotate[i]);
        }
#endif

    } while(0);

    //ret = json_object_put(jobj);
    //ret = json_object_put(hobj);

    return ret;
}

gint ParserClass::arg_parser(int *argc, char **argv[])
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
    //arg* arg = (arg *)data;
    GOptionContext *ctx;
    GError *err = NULL;
    gchar str[128];
    gint ret = 0;

    GOptionEntry entries[] = {
        {"mode", 'M', 0, G_OPTION_ARG_INT, &arg.ioMode, "io mode select 0(auto), 1(rw), 2(mmap), 3(userptr), 4(dmabuf), 5(dmabuf-import), default(0)", "INT"},
        {"test", 't', 0, G_OPTION_ARG_INT, &arg.testMode, "test mode select 0(normal), 1(test), default(0)", "INT"},
        {"debug", 'd', 0, G_OPTION_ARG_INT, &arg.dbg_level, "debug level, default(5)", "INT"},
        {"log", 'l', 0, G_OPTION_ARG_INT, &arg.log_level, "log level, default(6)", "INT"},
        {"dot", 'T', 0, G_OPTION_ARG_STRING, &arg.dotDir, "save dot representation of pipeline to FILE and exit, default(/tmp)", "STRING"},
        {"channel", 'a', 0, G_OPTION_ARG_INT, &arg.ch_enable, "cam channel enable bit, default(0x0f)", "HEX"},
        {"rotation", 'A', 0, G_OPTION_ARG_INT, &arg.ch_rotate, "cam channel rotation bit, default(0x00)", "HEX"},
        {"output", 'O', 0, G_OPTION_ARG_STRING, &arg.mntDir, "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
        {"width", 'w', 0, G_OPTION_ARG_INT, &arg.width, "cam width HD(1280), FHD(1920), default(1920)", "INT"},
        {"height", 'h', 0, G_OPTION_ARG_INT, &arg.height, "cam height HD(720), FHD(1080), default(1080)", "INT"},
        {"fmain", 'm', 0, G_OPTION_ARG_INT, &arg.main_fps, "main frame per second, default(15)", "INT"},
        {"frec", 'f', 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC], "record frame per second, default(15)", "INT"},
        {"frtsp", 'F', 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP], "rtsp frame per second, default(15)", "INT"},
        {"brec", 'b', 0, G_OPTION_ARG_INT, &arg.bps[STREAM_REC], "record Kbyte per second, default(4096)", "INT"},
        {"brtsp", 'B', 0, G_OPTION_ARG_INT, &arg.bps[STREAM_RTSP], "rtsp Kbyte per second, default(1024)", "INT"},
        {"oht", 'o', 0, G_OPTION_ARG_STRING, &arg.ohtName, "oht name, default(APPNAME)", "STRING"},
        {"delay", 'D', 0, G_OPTION_ARG_INT, &arg.play_delay, "from pause to play delay, default(0)", "SECOND"},
        {"fault", 0, 0, G_OPTION_ARG_NONE, &arg.fault, "no fault setup, default(FALSE)", "NONE"},
        {"duration", 's', 0, G_OPTION_ARG_INT, &arg.duration, "recoding file split duration, default(1)", "MINUTE"},
        {"erec", 'r', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_REC], "video recording enable, default(1)", "INT"},
        {"ertsp", 'R', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_RTSP], "rtsp streaming enable, default(1)", "INT"},
        {"eaudio", 'u', 0, G_OPTION_ARG_NONE, &arg.audio_en, "audio recording enable, default(FALSE)", "NONE"},
        {"ecap", 'c', 0, G_OPTION_ARG_NONE, &arg.capture_en, "video capturing enable, default(FALSE)", "NONE"},
        {"ein", 'i', 0, G_OPTION_ARG_NONE, &arg.input_en, "terminal input enable, default(FALSE)", "NONE"},
        {"port", 'p', 0, G_OPTION_ARG_STRING, &arg.rtsp_port, "rtsp port number, default(8554)", "STRING"},
        {"id", 'I', 0, G_OPTION_ARG_STRING, &arg.rtsp_id, "rtsp id, default(semes)", "STRING"},
        {"passwd", 'P', 0, G_OPTION_ARG_STRING, &arg.rtsp_passwd, "rtsp passwd, default(semes)", "STRING"},
        {"cmax", 'x', 0, G_OPTION_ARG_INT, &arg.captureMaxCnt, "capture max count, default(3)", "INT"},
        {"eover", 'v', 0, G_OPTION_ARG_NONE, &arg.overlay_en, "overlay enable, default(FALSE)", "NONE"},
        {"split", 'n', 0, G_OPTION_ARG_INT, &arg.split_margin_msec, "split margin msec, default(100)", "INT"},
        {"split", 'S', 0, G_OPTION_ARG_INT, &arg.split_max_msec, "split max msec, default(2000)", "INT"},
        {"grec", 'g', 0, G_OPTION_ARG_INT, &arg.gop[STREAM_REC], "rec gop size, default(15)", "INT"},
        {"grtsp", 'G', 0, G_OPTION_ARG_INT, &arg.gop[STREAM_RTSP], "rtsp gop size, default(15)", "INT"},
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

    if(arg.width != 1920 && arg.width != 1280)
    {
        __LOG(LOG_ERR, "[GST][%s:%d] width %d not supported", _FILE_, __LINE__, arg.width);
        return ret;
    }

    if(arg.height != 1080 && arg.height != 720)
    {
        __LOG(LOG_ERR, "[GST][%s:%d] height %d not supported", _FILE_, __LINE__, arg.height);
        return ret;
    }

    //__LOG(LOG_CRIT, "[GST][%s:%d] arg.ch_enable : 0x%02x", _FILE_, __LINE__, arg.ch_enable);
    for(guint8 i=0; i<MAX_CHANNEL; i++)
    {
        if(arg.ch_enable & (0x1 << i)) arg.cam_en[i] = TRUE;
        else arg.cam_en[i] = FALSE;
        if(arg.ch_rotate & (0x1 << i)) arg.cam_rotate[i] = TRUE;
        else arg.cam_rotate[i] = FALSE;
        //__LOG(LOG_CRIT, "[GST][%s:%d] arg.cam_en : %d", _FILE_, __LINE__, arg.cam_en[i]);
    }

    sprintf(str, "echo %d > %s", (arg.cam_rotate[1]<<1 | arg.cam_rotate[0]) & 0x3, DEFAULT_ROTATE_PATH_01);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[DSK][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return ret;
    }

    sprintf(str, "echo %d > %s", (arg.cam_rotate[3]<<1 | arg.cam_rotate[2]) & 0x3, DEFAULT_ROTATE_PATH_23);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[DSK][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return ret;
    }

    return ret;
}

void ParserClass::init_arg(gchar *argv)
{
    arg.appname = CHARNEXT(argv, '/');
    arg.ch_enable = DEFAULT_CH_ENABLE;
    arg.ch_rotate = DEFAULT_CH_ROTATE;
    arg.log_level = DEFAULT_LOG_LEVEL;
    arg.dbg_level = DEFAULT_DBG_LEVEL;
    arg.mntDir = DEFAULT_MOUNT_PATH;
    arg.duration = DEFUALT_DURATION;
    arg.rtsp_port = DEFAULT_RTSP_PORT;
    arg.rtsp_id = DEFAULT_RTSP_ID;
    arg.rtsp_passwd = DEFAULT_RTSP_PASSWD;
    arg.main_fps = DEFAULT_MAIN_FPS;
    arg.fps[STREAM_REC] = DEFAULT_RECORD_FPS;
    arg.fps[STREAM_RTSP] = DEFAULT_RTSP_FPS;
    arg.bps[STREAM_REC] = DEFAULT_RECORD_BITRATE;
    arg.bps[STREAM_RTSP] = DEFAULT_RTSP_BITRATE;
    arg.ohtName = arg.appname;
    arg.width = DEFAULT_WIDTH;
    arg.height = DEFAULT_HEIGHT;

    arg.ioMode = USERPTR_MODE;
    arg.testMode = NORMAL_MODE;
    arg.dotDir = DEFAULT_DOT_PATH;
    arg.captureDir = DEFAULT_CAPTURE_PATH;
    arg.play_delay = DEFAULT_PLAY_DELAY;
    arg.fault = FALSE;
    arg.audio_en = FALSE;
    arg.capture_en = FALSE;
    arg.captureMaxCnt = DEFAULT_CAPTURE_MAX_CNT;
    arg.input_en = FALSE;
    arg.overlay_en = FALSE;
    arg.split_margin_msec = DEFAULT_SPLIT_MARGIN_MSEC;
    arg.split_max_msec = DEFAULT_SPLIT_MAX_MSEC;
    arg.stream_en[STREAM_REC] = TRUE;
    arg.stream_en[STREAM_RTSP] = TRUE;
    arg.gop[STREAM_REC] = DEFAULT_GOP_SIZE;
    arg.gop[STREAM_RTSP] = DEFAULT_GOP_SIZE;

    for(guint8 i=0; i<MAX_CHANNEL; i++)
    {
        arg.cam_en[i] = FALSE;
        arg.cam_rotate[i] = FALSE;
    }
}

void ParserClass::print_option()
{
    const gchar *ioModeStr[6] = {"auto", "rw", "mmap", "useptr", "dmabuf", "dmabuf-import"};
#if 0
    g_print("oht name : %s\n", arg.ohtName);
    g_print("io mode : %s\n", ioModeStr[arg.ioMode]);
    g_print("test mode : %d\n", arg.testMode);
    g_print("log_level : %d\n", arg.log_level);
    g_print("debug_level : %d\n", arg.dbg_level);
    g_print("mount directory : %s\n", arg.mntDir);
    g_print("save dot directory : %s\n", arg.dotDir);
    g_print("save capture directory : %s\n", arg.captureDir);
    //g_print("ch_enable : 0x%02x\n", arg.ch_enable);
    g_print("record stream enable : %s\n", arg.stream_en[STREAM_REC]? "TURE":"FALSE");
    g_print("rtsp stream enable : %s\n", arg.stream_en[STREAM_RTSP]? "TURE":"FALSE");
    g_print("audio record enable : %s\n", arg.audio_en? "TURE":"FALSE");
    g_print("captrue enable : %s\n", arg.capture_en? "TURE":"FALSE");
    g_print("captrue max count : %d\n", arg.captureMaxCnt);
    g_print("terminal input enable : %s\n", arg.input_en? "TURE":"FALSE");
    //g_print("res mode : %s\n", arg.resMode? "HD":"FHD");
    //g_print("width : %d\n", arg.res[arg.resMode].width);
    //g_print("height : %d\n", arg.res[arg.resMode].height);
    g_print("width : %d\n", arg.width);
    g_print("height : %d\n", arg.height);
    g_print("main fps : %d\n", arg.main_fps);
    g_print("rec fps : %d\n", arg.fps[STREAM_REC]);
    g_print("rtsp fps : %d\n", arg.fps[STREAM_RTSP]);
    g_print("rec bitrate : %d\n", arg.bps[STREAM_REC]);
    g_print("rtsp bitrate : %d\n", arg.bps[STREAM_RTSP]);
    g_print("rec gop size : %d\n", arg.gop[STREAM_REC]);
    g_print("rtsp gop size : %d\n", arg.gop[STREAM_RTSP]);
    g_print("play delay : %d\n", arg.play_delay);
    g_print("no fault : %s\n", arg.fault? "TURE":"FALSE");
    g_print("duration : %d\n", arg.duration);
    g_print("rtsp port : %s\n", arg.rtsp_port);
    g_print("rtsp id : %s\n", arg.rtsp_id);
    g_print("rtsp passwd : %s\n", arg.rtsp_passwd);
    g_print("overlay enable : %s\n", arg.overlay_en? "TURE":"FALSE");
    g_print("split margin msec : %d\n", arg.split_margin_msec);
    g_print("split max msec : %d\n", arg.split_max_msec);
    for(guint8 i=0; i<MAX_CHANNEL; i++){
        //g_print("rec ch %d rotation : %d\n", i, arg.recRotationMode[i]);
        //g_print("rtsp ch %d rotation : %d\n", i, arg.rtspRotationMode[i]);
        g_print("cam_en ch %d : %s\n", i, arg.cam_en[i]? "TRUE":"FALSE");
        g_print("cam_rotate ch %d : %s\n", i, arg.cam_rotate[i]? "TRUE":"FALSE");
    }
#endif
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] 0 : %s, 1 : %s, 2 : %s", _FILE_, __LINE__, test0, test1, test2);
    __LOG(LOG_NOTICE, "[GST][%s:%d] io:%s, test:%d, noFault:%d, delay:%d, logLevel:%d, dbgLevel:%d, mntDir:%s, dotDir:%s, ", \
    _FILE_, __LINE__, ioModeStr[arg.ioMode], arg.testMode, arg.fault, arg.play_delay, arg.log_level, arg.dbg_level, arg.mntDir, arg.dotDir);

    __LOG(LOG_NOTICE, "[GST][%s:%d] oht_name:%s, duration:%d, width:%d, height:%d, mainFps:%d", _FILE_, __LINE__, \
                    arg.ohtName, arg.duration, arg.width, arg.height, arg.main_fps);
                    
    __LOG(LOG_NOTICE, "[GST][%s:%d] recEn:%d, rtspEn:%d, audoEn:%d, capEn:%d inputEn:%d, overlayEn:%d ", \
    _FILE_, __LINE__, arg.stream_en[STREAM_REC], arg.stream_en[STREAM_RTSP], arg.audio_en, arg.capture_en, arg.input_en, arg.overlay_en);

    if(arg.stream_en[STREAM_REC]) __LOG(LOG_NOTICE, "[GST][%s:%d] recFps:%d, recBps:%d, recGop:%d, splitMargin:%d, splitMax:%d", _FILE_, __LINE__, \
                                        arg.fps[STREAM_REC], arg.bps[STREAM_REC], arg.gop[STREAM_REC], arg.split_margin_msec, arg.split_max_msec);
    if(arg.stream_en[STREAM_RTSP]) __LOG(LOG_NOTICE, "[GST][%s:%d] rtspFps:%d, rtspBps:%d, rtspGop:%d, rtspID:%s, rtspPW:%s, rtspPort:%s", _FILE_, __LINE__, \
                                        arg.fps[STREAM_RTSP], arg.bps[STREAM_RTSP], arg.gop[STREAM_RTSP], arg.rtsp_id, arg.rtsp_passwd, arg.rtsp_port);
    if(arg.capture_en) __LOG(LOG_NOTICE, "[GST][%s:%d] captureMaxCnt:%d, capDir:%s", _FILE_, __LINE__, arg.captureMaxCnt, arg.captureDir);

    __LOG(LOG_NOTICE, "[GST][%s:%d] cam_en[0]:%d, rotate[0]:%d, cam_en[1]:%d, rotate[1]:%d, cam_en[2]:%d, rotate[2]:%d, cam_en[3]:%d, rotate[3]:%d", \
    _FILE_, __LINE__, arg.cam_en[0], arg.cam_rotate[0], arg.cam_en[1], arg.cam_rotate[1], arg.cam_en[2], arg.cam_rotate[2], arg.cam_en[3], arg.cam_rotate[3]);

    return;
}

gint ParserClass::cmd_parser(gchar* buffer, gpointer data)
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
