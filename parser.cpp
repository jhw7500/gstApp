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
#include "ipc.h"

#define LOG_KEY "CFG"


void ParserClass::init_arg(gchar *argv)
{
    //g_print("%s\n", __FUNCTION__);
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
    arg.main_fps[CSI_1] = DEFAULT_MAIN_FPS;
    arg.main_fps[CSI_2] = DEFAULT_MAIN_FPS;

    arg.ohtName = arg.appname;
    arg.width = DEFAULT_WIDTH;
    arg.height = DEFAULT_HEIGHT;

    arg.ioMode = IO_AUTO;
    arg.levelMode = MODE_NORMAL;
    arg.dotDir = DEFAULT_DOT_PATH;
    arg.captureDir = DEFAULT_CAPTURE_PATH;
    arg.play_delay = DEFAULT_PLAY_DELAY;
    arg.fault = FALSE;
    arg.audio_en = FALSE;
    arg.captureMaxCnt = DEFAULT_CAPTURE_MAX_CNT;
    arg.input_en = FALSE;
    arg.overlay_en = FALSE;
    arg.split_diff_msec = DEFAULT_SPLIT_DIFF_MSEC;
    arg.split_max_msec = DEFAULT_SPLIT_MAX_MSEC;
    arg.split_audio_min_msec = DEFAULT_SPLIT_AUDIO_MIN_MSEC;
    arg.split_sec = DEFAULT_SPLIT_SEC;
    arg.stream_en[STREAM_REC] = TRUE;
    arg.stream_en[STREAM_RTSP] = TRUE;
    arg.stream_en[STREAM_CAP] = FALSE;

    arg.cap_encoder_en = FALSE;
    arg.cap_always = FALSE;
    arg.cap_res_en = FALSE;
    arg.cap_delay = 0;
    arg.tcp_en = FALSE;
    arg.tcp_port = DEFAULT_TCP_PORT;

    arg.ipc_en = FALSE;
    arg.ipc_mid = DEFAULT_IPC_MID;

    arg.i2cConfig[CSI_1].exp_time = DEFAULT_EXP_TIME;
    arg.i2cConfig[CSI_2].exp_time = DEFAULT_EXP_TIME;
    for(guint8 i=0; i<MAX_CHANNEL; i++)
    {
        arg.camConfig[i].enable = FALSE;
        arg.camConfig[i].hflip = FALSE;
        arg.camConfig[i].vflip = FALSE;
        arg.camConfig[i].bps[0] = DEFAULT_RECORD_BITRATE;
        arg.camConfig[i].bps[1] = DEFAULT_RECORD_BITRATE;
        arg.camConfig[i].gop[0] = DEFAULT_GOP_SIZE;
        arg.camConfig[i].gop[1] = DEFAULT_GOP_SIZE;
        arg.camConfig[i].ae_on = TRUE;
        arg.camConfig[i].ae_gain = DEFAULT_AE_GAIN;
        arg.camConfig[i].awb = DEFAULT_AWB;
        arg.camConfig[i].iso = DEFAULT_ISO;
        arg.camConfig[i].lsc = DEFAULT_LSC;
        arg.camConfig[i].exp_time = DEFAULT_EXP_TIME;
#if 0
        arg.cam_en[i] = FALSE;
        arg.hflip[i] = FALSE;
        arg.vflip[i] = FALSE;
        arg.fps[STREAM_REC][i] = DEFAULT_RECORD_FPS;
        arg.fps[STREAM_RTSP][i] = DEFAULT_RTSP_FPS;
        arg.fps[STREAM_CAP][i]= DEFAULT_CAPTURE_FPS;
        arg.bps[STREAM_REC][i] = DEFAULT_RECORD_BITRATE;
        arg.bps[STREAM_RTSP][i] = DEFAULT_RTSP_BITRATE;
        arg.gop[STREAM_REC][i] = DEFAULT_GOP_SIZE;
        arg.gop[STREAM_RTSP][i] = DEFAULT_GOP_SIZE;
#endif
    }
}

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
    json_object *vobj;

    vobj = json_object_object_get(hobj, name);

    enum json_type type = json_object_get_type(vobj);

    if(type == json_type_object) {
        gchar **val = (gchar**)data;
        *val = (gchar*)json_object_get_string(vobj);
        __LOG(LOG_ERR, "[CFG][%s:%d] Type: Json object, name: %s, val: %s", _FILE_, __LINE__, name, *val);
        //return json_object_get_value(vobj, name, data);
    }
    else if(type == json_type_string) {
        gchar **val = (gchar**)data;
        *val = (gchar*)json_object_get_string(vobj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_int) {
        gint *val = (gint *)data;
        *val = json_object_get_int(vobj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s : %d", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_boolean) {
        gboolean *val = (gboolean *)data;
        *val = json_object_get_boolean(vobj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name, *val? "TRUE":"FALSE");
    }
    else if(type == json_type_double) {
        gdouble *val = (gdouble *)data;
        *val = json_object_get_double(vobj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s : %f", _FILE_, __LINE__, name, *val);
    }
    else if(type == json_type_array) {
        array_list *arr = json_object_get_array(vobj);
        //g_print("len:%ld arr->size:%ld arr->len:%ld\n", json_object_array_length(vobj), arr->size, arr->length);
        for(size_t i=0; i<arr->length; i++)
        {
            json_object *retrieved_obj = (json_object *)array_list_get_idx(arr, i);
            type = json_object_get_type(retrieved_obj);
            if(type == json_type_string) {
                gchar **arr = (gchar**)data;
                arr[i] = (gchar*)json_object_get_string(retrieved_obj);
                __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %s", _FILE_, __LINE__, name, i, arr[i]);
            }
            else if(type == json_type_int) {
                gint *arr = (gint *)data;
                arr[i] = json_object_get_int(retrieved_obj);
                __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %d", _FILE_, __LINE__, name, i, arr[i]);
            }
            else if(type == json_type_boolean) {
                gboolean *arr = (gboolean *)data;
                arr[i] = json_object_get_boolean(retrieved_obj);
                __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %s", _FILE_, __LINE__, name, i, arr[i]? "TRUE":"FALSE");
            }
            else if(type == json_type_double) {
                gdouble *arr = (gdouble *)data;
                arr[i] = json_object_get_double(retrieved_obj);
                __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %f", _FILE_, __LINE__, name, i, arr[i]);
            }
            else if(type == json_type_null)
            {
                __LOG(LOG_ERR, "[CFG][%s:%d] not exist : %s", _FILE_, __LINE__, name);
            }
            else
            {
                __LOG(LOG_ERR, "[CFG][%s:%d] unsupport type : %d", _FILE_, __LINE__, type);
            }
        }
    }
    else if(type == json_type_null)
    {
        __LOG(LOG_ERR, "[CFG][%s:%d] not exist : %s", _FILE_, __LINE__, name);
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

gint ParserClass::json_sub_object_get_value(const gchar *file, const gchar *header, const gchar *sub_obj, const gchar *name, gpointer data)
{
    gint ret = -1;
	json_object *jobj = NULL;
    json_object *hobj = NULL;
    json_object *sobj = NULL;

    jobj = json_object_from_file(file);
    enum json_type type = json_object_get_type(jobj);

	do {
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_, __LINE__, type);
			break;
		}

        //hobj = json_find_obj(jobj, "VHL_CAM");
        hobj = json_object_object_get(jobj, header);
        type = json_object_get_type(hobj);
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_, __LINE__, type);
			break;
		}

        sobj = json_object_object_get(hobj, sub_obj);
        type = json_object_get_type(hobj);
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_, __LINE__, type);
			break;
		}
        json_object_get_value(sobj, name, data);
    } while(0);

    return 0;
}

gint ParserClass::json_parser(const gchar *path, const gchar *header)
{
	gint ret = -1;

	json_object *jobj = NULL;
    json_object *hobj = NULL;
    json_object *sobj = NULL;
    json_object *vobj = NULL;
	//const gchar* ptr;

    arg.json_file = search_file(path, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
    __LOG(LOG_NOTICE, "[%s][%s:%d] json file name : %s", LOG_KEY, _FILE_, __LINE__, arg.json_file);

    if(strstr(arg.json_file, JSON_NAME_PREFIX) == NULL || strstr(arg.json_file, JSON_NAME_SUFFIX) == NULL) {
        __LOG(LOG_CRIT, "[%s][%s:%d] json file name not match %s %s", LOG_KEY, _FILE_, __LINE__, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
        return ret;
    }

    jobj = json_object_from_file(arg.json_file);
    enum json_type type = json_object_get_type(jobj);

	do {
		if(type != json_type_object) {
			__LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_, __LINE__, type);
			break;
		}

        //hobj = json_find_obj(jobj, "VHL_CAM");
        hobj = json_object_object_get(jobj, header);
        type = json_object_get_type(hobj);

		if(type != json_type_object) {
			__LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_, __LINE__, type);
			//break;
		}

        json_object_get_value(hobj, "vhl_name", &arg.ohtName);
        json_object_get_value(hobj, "id", &arg.rtsp_id);
        json_object_get_value(hobj, "cam_width", &arg.width);
        json_object_get_value(hobj, "cam_height", &arg.height);
        json_object_get_value(hobj, "recording_time", &arg.duration);
        json_object_get_value(hobj, "log_level", &arg.log_level);
        json_object_get_value(hobj, "debug_level", &arg.dbg_level);
        json_object_get_value(hobj, "fps", &arg.main_fps[CSI_1]);
        json_object_get_value(hobj, "fps", &arg.main_fps[CSI_2]);
#if 0
        json_object_get_value(hobj, "rec_fps", &arg.fps[STREAM_REC]);
        json_object_get_value(hobj, "rec_bps", &arg.bps[STREAM_REC]);
        json_object_get_value(hobj, "rtsp_fps", &arg.fps[STREAM_RTSP]);
        json_object_get_value(hobj, "rtsp_bps", &arg.bps[STREAM_RTSP]);
        json_object_get_value(hobj, "cap_fps", &arg.fps[STREAM_CAP]);
        json_object_get_value(hobj, "cam_en", &arg.cam_en);
        json_object_get_value(hobj, "hflip", &arg.hflip);
        json_object_get_value(hobj, "vflip", &arg.vflip);
#endif

        sobj = json_object_object_get(hobj, JSON_CAP_OBJ_NAME);
        json_object_get_value(sobj, "enable", &arg.stream_en[STREAM_CAP]);
        json_object_get_value(sobj, "delay", &arg.cap_delay);

        for(guint8 i=0; i<MAX_CHANNEL; i++)
        {
            sobj = json_object_object_get(hobj, g_strdup_printf("i2c%d", i/2? 1:2));
            json_object_get_value(sobj, "exp_time", &arg.i2cConfig[i/2].exp_time);
            arg.camConfig[i].exp_time = arg.i2cConfig[i/2].exp_time;
            vobj = json_object_object_get(sobj, g_strdup_printf("ch%d", i));
            json_object_get_value(vobj, "enable", &arg.camConfig[i].enable);
            json_object_get_value(vobj, "hflip", &arg.camConfig[i].hflip);
            json_object_get_value(vobj, "vflip", &arg.camConfig[i].vflip);
            json_object_get_value(vobj, "bps", &arg.camConfig[i].bps);
            json_object_get_value(vobj, "ae_on", &arg.camConfig[i].ae_on);
            json_object_get_value(vobj, "ae_gain", &arg.camConfig[i].ae_gain);
            //json_object_get_value(vobj, "exp_time", &arg.camConfig[i].exp_time);
            //json_object_get_value(vobj, "gop", &arg.camConfig[i].gop);
            //json_object_get_value(vobj, "awb", &arg.camConfig[i].awb);
            //json_object_get_value(vobj, "lsc", &arg.camConfig[i].lsc);
            //json_object_get_value(vobj, "iso", &arg.camConfig[i].iso);
            
            arg.ch_enable |= (arg.camConfig[i].enable<<i);
            arg.ch_rotate |= (arg.camConfig[i].hflip<<(i*2));
            arg.ch_rotate |= (arg.camConfig[i].vflip<<(i*2+1));
            //arg.ch_enable |= (arg.cam_en[i]<<i);
            //arg.ch_rotate |= (arg.hflip[i]<<(i*2));
            //arg.ch_rotate |= (arg.vflip[i]<<(i*2+1));
            for(guint8 k=0; k<MAX_MODE; k++)
                arg.fps[k][i]= arg.main_fps[CSI_1];
        }

    } while(0);

    //ret = json_object_put(jobj);
    //ret = json_object_put(hobj);

    return 0;
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
    guint8 i;

    GOptionEntry entries[] = {
        {"mode", 'm', 0, G_OPTION_ARG_INT, &arg.ioMode, "io mode select 0(auto), 1(rw), 2(mmap), 3(userptr), 4(dmabuf), 5(dmabuf-import), default(0)", "INT"},
        {"test", 'T', 0, G_OPTION_ARG_INT, &arg.levelMode, "test mode select 0(normal), 1(test), default(0)", "INT"},
        {"dbg", 'g', 0, G_OPTION_ARG_INT, &arg.dbg_level, "debug level, default(5)", "INT"},
        {"log", 'l', 0, G_OPTION_ARG_INT, &arg.log_level, "log level, default(6)", "INT"},
        {"dot", 'o', 0, G_OPTION_ARG_STRING, &arg.dotDir, "save dot representation of pipeline to FILE and exit, default(/tmp)", "STRING"},
        {"channel", 'c', 0, G_OPTION_ARG_INT, &arg.ch_enable, "cam channel enable bit, default(0x0f)", "HEX"},
        {"rotation", 'r', 0, G_OPTION_ARG_INT, &arg.ch_rotate, "cam channel rotation bit, default(0x00)", "HEX"},
        {"mnt", 'O', 0, G_OPTION_ARG_STRING, &arg.mntDir, "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
        {"width", 'w', 0, G_OPTION_ARG_INT, &arg.width, "cam width HD(1280), FHD(1920), default(1920)", "INT"},
        {"height", 'h', 0, G_OPTION_ARG_INT, &arg.height, "cam height HD(720), FHD(1080), default(1080)", "INT"},
        {"oht", 'n', 0, G_OPTION_ARG_STRING, &arg.ohtName, "oht name, default(APPNAME)", "STRING"},
        {"delay", 'd', 0, G_OPTION_ARG_INT, &arg.play_delay, "from pause to play delay, default(0)", "SECOND"},
        {"duration", 't', 0, G_OPTION_ARG_INT, &arg.duration, "recoding file split duration, default(1)", "MINUTE"},
        {"rec", 'e', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_REC], "video recording enable, default(1)", "INT"},
        {"rtsp", 'E', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_RTSP], "rtsp streaming enable, default(1)", "INT"},
        {"cap", 'a', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_CAP], "video capturing enable, default(0)", "INT"},
        {"audio", 's', 0, G_OPTION_ARG_INT, &arg.audio_en, "audio recording enable, default(FALSE)", "INT"},
        {"capenc", 'N', 0, G_OPTION_ARG_INT, &arg.cap_encoder_en, "video capture encoder(jpeg) enable, default(FALSE)", "INT"},
        {"capalways", 'y', 0, G_OPTION_ARG_INT, &arg.cap_always, "video capture bin always add, default(FALSE)", "INT"},
        {"capres", 'R', 0, G_OPTION_ARG_INT, &arg.cap_res_en, "video capture response enable, default(FALSE)", "INT"},
        {"capdelay", 'A', 0, G_OPTION_ARG_INT, &arg.cap_delay, "video capture delay(msec), default(0)", "INT"},
        {"etcp", 'C', 0, G_OPTION_ARG_INT, &arg.tcp_en, "tcp server enable, default(FALSE)", "INT"},
        {"ein", 'i', 0, G_OPTION_ARG_INT, &arg.input_en, "terminal input enable, default(FALSE)", "INT"},
        {"rport", 'P', 0, G_OPTION_ARG_STRING, &arg.rtsp_port, "rtsp port number, default(8554)", "STRING"},
        {"id", 'u', 0, G_OPTION_ARG_STRING, &arg.rtsp_id, "rtsp id, default(user)", "STRING"},
        {"passwd", 'p', 0, G_OPTION_ARG_STRING, &arg.rtsp_passwd, "rtsp passwd, default(user)", "STRING"},
        {"cmax", 'x', 0, G_OPTION_ARG_INT, &arg.captureMaxCnt, "capture max count, default(3)", "INT"},
        {"eover", 'v', 0, G_OPTION_ARG_INT, &arg.overlay_en, "overlay enable, default(FALSE)", "INT"},
        {"split_diff", 'D', 0, G_OPTION_ARG_INT, &arg.split_diff_msec, "split diff msec, default(100)", "INT"},
        {"split_max", 'X', 0, G_OPTION_ARG_INT, &arg.split_max_msec, "split max msec, default(2000)", "INT"},
        {"split_sec", 'S', 0, G_OPTION_ARG_INT, &arg.split_sec, "split sec, default(0)", "INT"},
        {"eipc", 'f', 0, G_OPTION_ARG_INT, &arg.ipc_en, "ipc enable, default(FALSE)", "INT"},
        {"ipc_mid", 'F', 0, G_OPTION_ARG_INT, &arg.ipc_mid, "ipc message id, default(0x65)", "INT"},
        {"fault", 0, 0, G_OPTION_ARG_INT, &arg.fault, "no fault setup, default(FALSE)", "INT"},
        {"tport", 0, 0, G_OPTION_ARG_INT, &arg.tcp_port, "tcp port num, default(8555)", "INT"},
        {"fmain0", 0, 0, G_OPTION_ARG_INT, &arg.main_fps[CSI_1], "csi1 main frame per second, default(15)", "INT"},
        {"fmain1", 0, 0, G_OPTION_ARG_INT, &arg.main_fps[CSI_2], "csi2 main frame per second, default(15)", "INT"},
        {"frec0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][0], "ch0 record frame per second, default(15)", "INT"},
        {"frec1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][1], "ch1 record frame per second, default(15)", "INT"},
        {"frec2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][2], "ch2 record frame per second, default(15)", "INT"},
        {"frec3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][3], "ch3 record frame per second, default(15)", "INT"},
        {"frtsp0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][0], "ch0 rtsp frame per second, default(15)", "INT"},
        {"frtsp1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][1], "ch1 rtsp frame per second, default(15)", "INT"},
        {"frtsp2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][2], "ch2 rtsp frame per second, default(15)", "INT"},
        {"frtsp3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][3], "ch3 rtsp frame per second, default(15)", "INT"},
        {"fcap0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][0], "ch0 capture frame per second, default(15)", "INT"},
        {"fcap1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][1], "ch1 capture frame per second, default(15)", "INT"},
        {"fcap2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][2], "ch2 capture frame per second, default(15)", "INT"},
        {"fcap3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][3], "ch3 capture frame per second, default(15)", "INT"},
        {"brec0", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[0].bps[STREAM_REC], "ch0 record Kbyte per second, default(4096)", "INT"},
        {"brec1", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[1].bps[STREAM_REC], "ch1 record Kbyte per second, default(4096)", "INT"},
        {"brec2", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[2].bps[STREAM_REC], "ch2 record Kbyte per second, default(4096)", "INT"},
        {"brec3", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[3].bps[STREAM_REC], "ch3 record Kbyte per second, default(4096)", "INT"},
        {"brtsp0", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[0].bps[STREAM_RTSP], "ch0 rtsp Kbyte per second, default(1024)", "INT"},
        {"brtsp1", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[1].bps[STREAM_RTSP], "ch1 rtsp Kbyte per second, default(1024)", "INT"},
        {"brtsp2", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[2].bps[STREAM_RTSP], "ch2 rtsp Kbyte per second, default(1024)", "INT"},
        {"brtsp3", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[3].bps[STREAM_RTSP], "ch3 rtsp Kbyte per second, default(1024)", "INT"},
        {"grec0", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[0].gop[STREAM_REC], "ch0 rec gop size, default(15)", "INT"},
        {"grec1", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[1].gop[STREAM_REC], "ch1 rec gop size, default(15)", "INT"},
        {"grec3", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[2].gop[STREAM_REC], "ch2 rec gop size, default(15)", "INT"},
        {"grec0", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[3].gop[STREAM_REC], "ch3 rec gop size, default(15)", "INT"},
        {"grtsp0", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[0].gop[STREAM_RTSP], "ch0 rtsp gop size, default(15)", "INT"},
        {"grtsp1", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[1].gop[STREAM_RTSP], "ch1 rtsp gop size, default(15)", "INT"},
        {"grtsp2", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[2].gop[STREAM_RTSP], "ch2 rtsp gop size, default(15)", "INT"},
        {"grtsp3", 0, 0, G_OPTION_ARG_INT, &arg.camConfig[3].gop[STREAM_RTSP], "ch3 rtsp gop size, default(15)", "INT"},
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    ret = g_option_context_parse(ctx, argc, argv, &err);
    if(!ret)
    {
        __LOG(LOG_CRIT, "[%s][%s:%d] Failed to initialize : %s", LOG_KEY, _FILE_, __LINE__, err->message);
        g_error_free(err);
        return ret;
    }

#if 1
    gint max[2] = {0, 0};

    for(i=0; i<3; i++)
    {
        for(guint8 k=0; k<MAX_CHANNEL; k++)
        {
            if(arg.fps[i][k] > max[k/2])
                max[k/2] = arg.fps[i][k];
        }
    }

    if(max[0] > 0) arg.main_fps[CSI_2] = max[0];
    if(max[1] > 0) arg.main_fps[CSI_1] = max[1];

    __LOG(LOG_NOTICE, "[%s][%s:%d] arg.main_fps[CSI_1]:%d, arg.main_fps[CSI_2]:%d", LOG_KEY, _FILE_, __LINE__, arg.main_fps[CSI_1], arg.main_fps[CSI_2]);
#endif

    //__LOG(LOG_CRIT, "[GST][%s:%d] arg.ch_enable : 0x%02x", _FILE_, __LINE__, arg.ch_enable);
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if((arg.ch_enable>>i & 0x1) == 0x01) arg.cam_en[i] = TRUE;
        else arg.cam_en[i] = FALSE;

        if((arg.ch_rotate>>(i*2) & 0x01) == 0x01) arg.hflip[i] = TRUE;
        else arg.hflip[i] = FALSE;

        if((arg.ch_rotate>>(i*2) & 0x02) == 0x02) arg.vflip[i] = TRUE;
        else arg.vflip[i] = FALSE;

        //__LOG(LOG_CRIT, "[GST][%s:%d] arg.cam_en : %d", _FILE_, __LINE__, arg.cam_en[i]);
    }

    sprintf(str, "echo %d > %s", arg.ch_enable&0x03, DEFAULT_ENABLE_PATH_01);
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", LOG_KEY, _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
        return ret;
    }

    sprintf(str, "echo %d > %s", (arg.ch_enable>>2)&0x03, DEFAULT_ENABLE_PATH_23);
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", LOG_KEY, _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
        return ret;
    }

    sprintf(str, "echo %d > %s", arg.ch_rotate&0x0f, DEFAULT_ROTATE_PATH_01);
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", LOG_KEY, _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
        return ret;
    }

    sprintf(str, "echo %d > %s", (arg.ch_rotate>>4)&0x0f, DEFAULT_ROTATE_PATH_23);
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", LOG_KEY, _FILE_, __LINE__, str);
    ret = system(str);
    if (ret < 0) {
        __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
        return ret;
    }

    return ret;
}

gint ParserClass::check_arg()
{
    const gchar *ioModeStr[6] = {"auto", "rw", "mmap", "useptr", "dmabuf", "dmabuf-import"};
    gint i = 0;

    //g_print("%s\n", __FUNCTION__);
#if 0
    g_print("oht name : %s\n", arg.ohtName);
    g_print("io mode : %s\n", ioModeStr[arg.ioMode]);
    g_print("level mode : %d\n", arg.levelMode);
    g_print("log_level : %d\n", arg.log_level);
    g_print("debug_level : %d\n", arg.dbg_level);
    g_print("mount directory : %s\n", arg.mntDir);
    g_print("save dot directory : %s\n", arg.dotDir);
    g_print("save capture directory : %s\n", arg.captureDir);
    //g_print("ch_enable : 0x%02x\n", arg.ch_enable);
    g_print("record stream enable : %s\n", arg.stream_en[STREAM_REC]? "TURE":"FALSE");
    g_print("rtsp stream enable : %s\n", arg.stream_en[STREAM_RTSP]? "TURE":"FALSE");
    g_print("captrue enable : %s\n", arg.stream_en[STREAM_CAP]? "TURE":"FALSE");
    g_print("audio record enable : %s\n", arg.audio_en? "TURE":"FALSE");
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
    g_print("split diff msec : %d\n", arg.split_diff_msec);
    g_print("split max msec : %d\n", arg.split_max_msec);
    for(guint8 i=0; i<MAX_CHANNEL; i++){
        //g_print("rec ch %d rotation : %d\n", i, arg.recRotationMode[i]);
        //g_print("rtsp ch %d rotation : %d\n", i, arg.rtspRotationMode[i]);
        g_print("cam_en ch %d : %s\n", i, arg.cam_en[i]? "TRUE":"FALSE");
        g_print("cam_rotate ch %d : %s\n", i, arg.cam_rotate[i]? "TRUE":"FALSE");
    }
#endif
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] 0 : %s, 1 : %s, 2 : %s", _FILE_, __LINE__, test0, test1, test2);
    __LOG(LOG_NOTICE, "[%s][%s:%d] io:%s, test:%d, noFault:%d, delay:%d, logLevel:%d, dbgLevel:%d, mntDir:%s, dotDir:%s, ", \
                        LOG_KEY, _FILE_, __LINE__, ioModeStr[arg.ioMode], arg.levelMode, arg.fault, arg.play_delay, arg.log_level, \
                        arg.dbg_level, arg.mntDir, arg.dotDir);

    __LOG(LOG_NOTICE, "[%s][%s:%d] oht_name:%s, duration:%d, width:%d, height:%d, csi1_fps:%d, csi2_fps:%d", LOG_KEY, _FILE_, __LINE__, \
                        arg.ohtName, arg.duration, arg.width, arg.height, arg.main_fps[CSI_1], arg.main_fps[CSI_2]);
                    
    __LOG(LOG_NOTICE, "[%s][%s:%d] chEn:0x%x, recEn:%d, rtspEn:%d, capEn:%d, audoEn:%d, inputEn:%d, overlayEn:%d tcpEn:%d, ipc_en:%d", \
                        LOG_KEY, _FILE_, __LINE__, arg.ch_enable, arg.stream_en[STREAM_REC], arg.stream_en[STREAM_RTSP], arg.stream_en[STREAM_CAP], \
                        arg.audio_en, arg.input_en, arg.overlay_en, arg.tcp_en, arg.ipc_en);
    __LOG(LOG_NOTICE, "[%s][%s:%d] rtspID:%s, rtspPW:%s, rtspPort:%s, splitSec:%d, splitMargin:%d, splitMax:%d", LOG_KEY, _FILE_, __LINE__, \
                        arg.rtsp_id, arg.rtsp_passwd, arg.rtsp_port, arg.split_sec, arg.split_diff_msec, arg.split_max_msec);

    if(arg.stream_en[STREAM_REC]) 
    {
        __LOG(LOG_NOTICE, "[%s][%s:%d] rec ch0 fps:%d, ch1 fps:%d, ch2 fps:%d, ch3 fps:%d", LOG_KEY, _FILE_, __LINE__, \
                            arg.fps[STREAM_REC][0], arg.fps[STREAM_REC][1], arg.fps[STREAM_REC][2], arg.fps[STREAM_REC][3]);  
    }

    if(arg.stream_en[STREAM_RTSP])
    {
        __LOG(LOG_NOTICE, "[%s][%s:%d] rtsp ch0 fps:%d, ch1 fps:%d, ch2 fps:%d, ch3 fps:%d", LOG_KEY, _FILE_, __LINE__, \
                            arg.fps[STREAM_RTSP][0], arg.fps[STREAM_RTSP][1], arg.fps[STREAM_RTSP][2], arg.fps[STREAM_RTSP][3]);  
    }

    if(arg.stream_en[STREAM_CAP])
    {
        __LOG(LOG_NOTICE, "[%s][%s:%d] capture ch0 fps:%d, ch1 fps:%d, ch2 fps:%d, ch3 fps:%d", LOG_KEY, _FILE_, __LINE__, \
                            arg.fps[STREAM_CAP][0], arg.fps[STREAM_CAP][1], arg.fps[STREAM_CAP][2], arg.fps[STREAM_CAP][3]);  
        __LOG(LOG_NOTICE, "[%s][%s:%d] capEncEn:%d captureAlways:%d, captureMaxCnt:%d, cap_res_en:%d, capDir:%s, cap_delay:%d", LOG_KEY, _FILE_, __LINE__, \
                            arg.cap_encoder_en, arg.cap_always, arg.captureMaxCnt, arg.cap_res_en, arg.captureDir, arg.cap_delay);
    }

    if(arg.tcp_en) __LOG(LOG_NOTICE, "[%s][%s:%d] tcpPort:%d", LOG_KEY, _FILE_, __LINE__, arg.tcp_port);
    if(arg.ipc_en) __LOG(LOG_NOTICE, "[%s][%s:%d] ipc_mid:%d", LOG_KEY, _FILE_, __LINE__, arg.ipc_mid);

    for(i=0; i<MAX_CHANNEL; i++) {
        __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d en:%s, vflip:%s, hflip:%s, bps:%d,%d, ae_on:%d, ae_gain:%d, exp_time:%d", \
            LOG_KEY, _FILE_, __LINE__, i, arg.camConfig[i].enable? "true":"false",  arg.camConfig[i].vflip? "true":"false", arg.camConfig[i].hflip? "true":"false", \
            arg.camConfig[i].bps[STREAM_REC], arg.camConfig[i].bps[STREAM_RTSP], arg.camConfig[i].ae_on, arg.camConfig[i].ae_gain, arg.camConfig[i].exp_time);

        if(arg.camConfig[i].bps[STREAM_REC] < 1 || arg.camConfig[i].bps[STREAM_RTSP] < 1) {
            __LOG(LOG_CRIT, "[%s][%s:%d] rec bps %d, rtsp bps %d not supported", LOG_KEY, _FILE_, __LINE__, \
                    arg.camConfig[i].bps[STREAM_REC], arg.camConfig[i].bps[STREAM_RTSP]);

            return -1;
        }
    }
    gint total_fps = 0;
    //total_fps += arg.stream_en[STREAM_REC]*arg.fps[STREAM_REC]*(arg.cam_en[0]+arg.cam_en[1]+arg.cam_en[2]+arg.cam_en[3]);
    //total_fps += arg.stream_en[STREAM_RTSP]*arg.fps[STREAM_RTSP]*(arg.cam_en[0]+arg.cam_en[1]+arg.cam_en[2]+arg.cam_en[3]);
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(arg.stream_en[STREAM_REC]) total_fps += arg.fps[STREAM_REC][i]*arg.cam_en[i];
        if(arg.stream_en[STREAM_RTSP]) total_fps += arg.fps[STREAM_RTSP][i]*arg.cam_en[i];
    }

    //total_fps += arg.stream_en[STREAM_CAP]*arg.fps[STREAM_CAP]*(arg.cam_en[0]+arg.cam_en[1]+arg.cam_en[2]+arg.cam_en[3]);
    total_fps += arg.stream_en[STREAM_CAP]*0;
    total_fps += arg.audio_en*0;

    if(arg.duration < 1) {
        __LOG(LOG_CRIT, "[%s][%s:%d] recording duration %d not supported", LOG_KEY, _FILE_, __LINE__, arg.duration);
        return -1;
    }

    if(arg.width == 1280 && arg.height == 720) {
        if(total_fps > MAX_FPS_HD)
        {
            __LOG(LOG_CRIT, "[%s][%s:%d] HD max fps over : total_fps(%d) > MAX_FPS_HD(%d)", LOG_KEY, _FILE_, __LINE__, total_fps, MAX_FPS_HD);
            return -1;
        }
    }
    else if(arg.width == 1920 && arg.height == 1080) {
        if(total_fps > MAX_FPS_FHD)
        {
            __LOG(LOG_CRIT, "[%s][%s:%d] FHD max fps over : total_fps(%d) > MAX_FPS_FHD(%d)", LOG_KEY, _FILE_, __LINE__, total_fps, MAX_FPS_FHD);
            return -1;
        }
    }
    else {
        __LOG(LOG_CRIT, "[%s][%s:%d] width %d not supported", LOG_KEY, _FILE_, __LINE__, arg.width);
        return -1;
    }

    __LOG(LOG_NOTICE, "[%s][%s:%d] total_fps : %d", LOG_KEY, _FILE_, __LINE__, total_fps);

    return 0;
}

static void captureThreadFunc(gpointer data)
{
    ThreadArgs *thraedArgs = (ThreadArgs *)data;
    VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
    //RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
    //RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
    //MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
    CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg4);
    GstState state;
    TCfiData *_TCfiData = (TCfiData *)(thraedArgs->arg5);
    //guint i = (guint)thraedArgs->arg5;
    guint k;

    guint i = _TCfiData->data.channel;
    //guint i = (guint)(thraedArgs->arg6);
    g_print("ch%d %s\n", i, __FUNCTION__);

    do  //if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
    {
        if (!captureBin[i].init(i, cmdArg.crop_en[i / 2]))
            g_print("ch%d captrueBin init failed\n", i);

        if (captureBin[i].addBinToPipe(pipeline))
            g_print("ch%d capture bin add\n", i);
        else
            g_print("ch%d capture bin add error\n", i);
        // g_usleep(10000);

        if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) != TRUE)
        {
            // g_print("ch%d capture not linked\n", key);
            if (gst_pad_link(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
            {
                g_print("ch%d capture link error!\n", i);
                // captureBin[i].removeBinToPipe(pipeline);
                break;
            }
            else
            {
                g_print("ch%d capture link ok!\n", i);
                // gst_element_sync_state_with_parent(captureBin[i].be.bin);
                // gst_element_set_state(pipeline, GST_STATE_PLAYING);
            }
        }
        else
        {
            g_print("ch%d capture already linked!\n", i);
            // gst_element_sync_state_with_parent(captureBin[i].be.bin);
            // gst_element_set_state(pipeline, GST_STATE_PLAYING);
        }

        for (guint k = 0; k < 500; k++)
        {
            g_print("pipeline playing\n");
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            // g_print("ch%d captrue state sync\n", key);
            // gst_element_sync_state_with_parent(captureBin[key].be.bin);
            // g_usleep(1000);
            state = captureBin[i].getState();
            if (state == GST_STATE_PLAYING)
                break;
            else
                g_print("state : %d\n", state);

            g_usleep(1000);
        }

        captureBin[i].setFilePath(_TCfiData->data.prefix);
        captureBin[i].startCapture(_TCfiData->data.cap_cnt);
        // g_print("ch:%d, fps:%d, mode : %d, max_cnt : %d\n", key, cmdArg.fps[STREAM_CAP][key], key1, key2);
        // captureBin[key].startCapture(key2);

        for (k = 0; k < 500; k++)
        {
            if (captureBin[i].getCaptureCnt() >= _TCfiData->data.cap_cnt)
                break;

            g_usleep(10000);
        }

        __LOG(LOG_NOTICE, "[GST][%s:%d] capture end", _FILE_, __LINE__);

        if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()))
        {
            g_print("ch%d capture unlink ok!\n", i);
            state = captureBin[i].getState();
            g_print("state : %d\n", state);
            for (k = 0; k < 500; k++)
            {
                captureBin[i].setState(GST_STATE_NULL);
                state = captureBin[i].getState();
                g_print("state : %d\n", i);
                if (state == GST_STATE_NULL)
                {
                    if (captureBin[i].removeBinToPipe(pipeline))
                        g_print("ch%d capture bin remove\n", i);
                    else
                        g_print("ch%d capture bin remove error\n", i);

                    break;
                }
                g_usleep(10000);
            }
        }
        else
            g_print("ch%d capture unlink err!\n", i);

        // captureBin[i].removeBinToPipe(pipeline);
    } while(0);
}

gint ParserClass::cfi_parser(gchar* buffer, gint len, gpointer data)
{
    ThreadArgs *thraedArgs = (ThreadArgs *)data;
    VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
    //RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
    //RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
    //MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
    CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg4);
    //CaptureBin *captureBin = (CaptureBin *)(data);
    //ThreadArgs *arg[2];
    CIPCInsance* ipcInstance = CIPCInsance::getInstance();

    TCfiData _TCfiData;
    guint i = 0;
    gint ret = 1;
    guint chk_cnt = 0;
    guint8 ch_en = 0;
    guint16 capMaxCnt = 0;

    //memset(_TCfiData.byte, 0, CFI_DATA_LEN);
    memcpy(_TCfiData.byte, buffer, len);

	if(_TCfiData.data.len != len) {
		__LOG(LOG_ERR, "[%s][%s:%d] header len %d != %d", CAP_LOG_KEY, _FILE_, __LINE__, _TCfiData.data.len, len);
		return -1;
	}

	if(len != CFI_DATA_LEN) {
		__LOG(LOG_ERR, "[%s][%s:%d] recv byte %d != %d", CAP_LOG_KEY, _FILE_, __LINE__, len, CFI_DATA_LEN);
		return -1;
	}

	if(_TCfiData.data.cmd_id != CFI_CAP_REQ_CMD_ID) {
		__LOG(LOG_ERR, "[%s][%s:%d] header cmd_id 0x%x != 0x%x", CAP_LOG_KEY, _FILE_, __LINE__, _TCfiData.data.cmd_id, CFI_CAP_REQ_CMD_ID);
		return -1;
	}
    
    //__LOG(LOG_INFO, "[CFI][%s:%d] prefix : %s", _FILE_, __LINE__, _TCfiData.data.prefix);
    ch_en = _TCfiData.data.channel;
    __LOG(LOG_NOTICE, "[%s][%s:%d] channel: 0x%x, cap_cnt: %d, prefix: %s", CAP_LOG_KEY, _FILE_, __LINE__, ch_en, _TCfiData.data.cap_cnt, _TCfiData.data.prefix);
    json_sub_object_get_value(cmdArg.json_file, JSON_CAM_OBJ_NAME, JSON_CAP_OBJ_NAME, "delay", &cmdArg.cap_delay);
    __LOG(LOG_NOTICE, "[%s][%s:%d] capture delay: %d msec", CAP_LOG_KEY, _FILE_, __LINE__, cmdArg.cap_delay);
    
    if(cmdArg.cap_delay) g_usleep(1000*cmdArg.cap_delay);

    for(i=0; i<MAX_CHANNEL; i++)
    {
        if((ch_en>>i & 0x1) != 0x01) continue;

        if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
        {
#if 1
            //gst_pad_remove_probe(captureBin[i].queue_src_pad, captureBin[i].probe_id);
            captureBin[i].setFilePath(_TCfiData.data.prefix);
            captureBin[i].startCapture(_TCfiData.data.cap_cnt);
#else
            ThreadArgs *args = g_new(ThreadArgs, 1);
            *args = *thraedArgs;
            TCfiData *data_copy = g_new(TCfiData, 1);
            *data_copy = _TCfiData;
            data_copy->data.channel = i;
            args->arg5 = data_copy;
            //__LOG(LOG_NOTICE, "[CFI][%s:%d] ch%d call captrue thread", _FILE_, __LINE__, i);
            g_print("ch%d call captrue thread\n", i);
            captureThread[i] = g_thread_new(g_strdup_printf("captureThread%d", i), (GThreadFunc)captureThreadFunc, args);
#endif
        }
    }
    
    capMaxCnt = _TCfiData.data.cap_cnt;
    if(cmdArg.cap_res_en)
    {
        _TCfiData.data.cmd_id = CFI_CAP_RES_CMD_ID;
        __LOG(LOG_NOTICE, "[%s][%s:%d] capture check for response", CAP_LOG_KEY, _FILE_, __LINE__);
        for(i=0; i<MAX_CHANNEL; i++)
        {
            if((ch_en>>i & 0x1) != 0x01) continue;

            if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
            {
                do
                {
                    _TCfiData.data.channel = 1 << i;
                    if(captureBin[i].getCaptureCnt() == capMaxCnt)
                    {
                        _TCfiData.data.cap_cnt = capMaxCnt;
                        ipcInstance->sendData((char *)_TCfiData.byte, CFI_DATA_LEN);
                        ret = 1;
                        break;
                    }
                    else if (chk_cnt > capMaxCnt*2)
                    {
                        _TCfiData.data.cap_cnt = capMaxCnt - captureBin[i].getCaptureCnt();
                        ipcInstance->sendData((char *)_TCfiData.byte, CFI_DATA_LEN);
                        ret = -1;
                        break;
                    }
                    chk_cnt++;
                    g_usleep(1000000);
                } while(1);
                //g_print("ch%d break\n", i);
            }
        }
    }

    return ret;
}

gint ParserClass::cmd_parser(gchar* buffer, gint len, gpointer data)
{
#define SPLIT_CHAR  " "

    gint i, key, key1, key2;
    gchar *token = NULL;
    const gchar *stateStr[] = {"PENDING", "NULL", "READY", "PAUSED", "PLAYING"};
    //const gchar *stateChangeReturnStr[] = {"GST_STATE_CHANGE_FAILURE", "GST_STATE_CHANGE_SUCCESS", "GST_STATE_CHANGE_ASYNC", "GST_STATE_CHANGE_NO_PREROLL"};
    ThreadArgs *thraedArgs = (ThreadArgs *)data;
    VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
    RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
    RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
    MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
    CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg4);
    GstState state;
    //GstStateChangeReturn stateRet;
    //GstPadLinkReturn linkRet;

    //gint len = strlen(buffer);
    buffer[len] = '\0';
    g_print("Input: %s\n", buffer);
    //__LOG(LOG_NOTICE, "[TCP][%s:%d] Input: %s", _FILE_, __LINE__, buffer);

    token = strtok(buffer, " ");
    if (compareBuf(token, "cmd", 3))
    {
        token = strtok(NULL, "\0");
        int ret;
        gchar* str = g_strdup_printf("bash -ic 'source /root/.bashrc; %s'", token);
        ret = system(str);
        if(ret) g_print("cmd error ret:%d\n", ret);

        g_free(str);
        return 0;
    }

#if 1
    for (i = 0; i < len; i++) {
        if(buffer[i] == '\0' || buffer[i] == '\n') buffer[i] = SPLIT_CHAR[0];
    }
#endif

    token = strtok(buffer, SPLIT_CHAR);

#if 1
    if (compareBuf(token, "get", 3))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "bps", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].getBitrate();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getBitrate();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "fps", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].getFps();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getFps();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                // for (i = 0; i < MAX_CHANNEL; i++)
                // if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC]) recordBin[i].getFps();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getCaps();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "rotate", 6))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].getRotation();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getRotation();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "state", 5))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        state = recordBin[i].getState();
                        g_print("rec ch%d state : %s\n", i, stateStr[state]);
                    }
                }
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        state = rtspServerBin[i].getState();
                        g_print("rtsp ch%d state : %s\n", i, stateStr[state]);
                    }
                }
            }
            else if (compareBuf(token, "cap", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        state = captureBin[i].getState();
                        g_print("capture ch%d state : %s\n", i, stateStr[state]);
                    }
                }
            }
            else if (compareBuf(token, "pipe", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                if (compareBuf(token, "0", 1))
                {
                    gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                    g_print("pipe state : %s\n", stateStr[state]);
                }
                else if (compareBuf(token, "1", 1))
                {
                    //gst_element_get_state(pipeline2, &state, NULL, GST_CLOCK_TIME_NONE);
                    g_print("pipe1 state : %s\n", stateStr[state]);
                }
                else
                    g_print("wrong cmd!\n");
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "iomode", 6))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "0", 1))
            {
                videoBin[0].getIoMode();
            }
            else if (compareBuf(token, "1", 1))
            {
                videoBin[1].getIoMode();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "gop", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].getGop();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getGop();
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "key", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].getKeyframe();
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].getKeyframe();
            }
            else
                g_print("wrong cmd!\n");
        }
        else
            g_print("wrong cmd!\n");
    } // get
#endif
#if 1
    else if (compareBuf(token, "set", 3))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "bps", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                i = charArrayToInt(token);
                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }
                
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 ||key > 9999)
                {
                    g_print("bps %d not supported\n", key);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC]) recordBin[i].setBitrate(key);
                //for (i = 0; i < MAX_CHANNEL; i++)
                    //if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        //recordBin[i].setBitrate(key);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 9999)
                {
                    g_print("bps %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].setBitrate(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "fps", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 99)
                {
                    g_print("fps %d not supported\n", key);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC]) recordBin[i].setFps(key);

                //for (i = 0; i < MAX_CHANNEL; i++)
                    //if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        //recordBin[i].setFps(key);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 99)
                {
                    g_print("fps %d not supported\n", key);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP]) rtspServerBin[i].setFps(key);
                //for (i = 0; i < MAX_CHANNEL; i++)
                    //if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        //rtspServerBin[i].setFps(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "rotate", 6))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 5)
                {
                    g_print("rotation %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].setRotation(key);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 5)
                {
                    g_print("rotation %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].setRotation(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "state", 5))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, "\0");
                key = charArrayToInt(token);

                if (key < 0 || key > 4)
                {
                    g_print("state %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        g_print("rec ch%d state : %s\n", i, stateStr[recordBin[i].getState()]);
                        if(recordBin[i].setState((GstState)key) == GST_STATE_CHANGE_FAILURE)
                        {
                            g_print("rec %s state change error\n", stateStr[key]);
                        }
                        else
                        {
                            g_print("rec ch%d state : %s\n", i, stateStr[recordBin[i].getState()]);
                        }
                    }
                }

                g_print("rec state : %s\n", stateStr[key]);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 4)
                {
                    g_print("state %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        g_print("rtsp ch%d state : %s\n", i, stateStr[rtspServerBin[i].getState()]);
                        if(rtspServerBin[i].setState((GstState)key) == GST_STATE_CHANGE_FAILURE)
                        {
                            g_print("rtsp %s state change error\n", stateStr[key]);
                        }
                        else
                        {
                            g_print("rtsp ch%d state : %s\n", i, stateStr[rtspServerBin[i].getState()]);
                        }
                    }
                }
                
                g_print("rtsp state : %s\n", stateStr[state]);
            }
            else if (compareBuf(token, "audio", 5))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);
                i = 4;

                if (key < 0 || key > 4)
                {
                    g_print("state %d not supported\n", key);
                    return -1;
                }

                if(cmdArg.audio_en)
                {
                    g_print("rtsp ch%d state : %s\n", i, stateStr[rtspServerBin[i].getState()]);
                    if(rtspServerBin[i].setState((GstState)key) == GST_STATE_CHANGE_FAILURE)
                    {
                        g_print("rtsp %s state change error\n", stateStr[key]);
                    }
                    else
                    {
                        g_print("rtsp ch%d state : %s\n", i, stateStr[rtspServerBin[i].getState()]);
                    }
                }
            }
            else if (compareBuf(token, "cap", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 4)
                {
                    g_print("state %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        g_print("before capture ch%d state : %s\n", i, stateStr[captureBin[i].getState()]);
                        g_print("capture ch%d state change ret : %d\n", i, captureBin[i].setState((GstState)key));
                        g_print("after capture ch%d state : %s\n", i, stateStr[captureBin[i].getState()]);
                    }
                }
                //g_print("capture state : %s\n", stateStr[state]);
            }
            else if (compareBuf(token, "pipe", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 4)
                {
                    g_print("state %d not supported\n", key);
                    return -1;
                }
                //g_print("before pipe state : %s\n", i, gst_element_get_state());
                gst_element_set_state(pipeline, (GstState)key);
                g_print("after pipe state : %s\n", stateStr[key]);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "iomode", 6))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "0", 1))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 5)
                {
                    g_print("ioMode %d not supported\n", key);
                    return -1;
                }

                videoBin[0].setIoMode(key);
            }
            else if (compareBuf(token, "1", 1))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 5)
                {
                    g_print("ioMode %d not supported\n", key);
                    return -1;
                }

                videoBin[1].setIoMode(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "gop", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 100)
                {
                    g_print("gop %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].setGop(key);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 100)
                {
                    g_print("gop %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].setGop(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "key", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rec", 3))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 1)
                {
                    g_print("key %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                        recordBin[i].setkeyframe(key);
            }
            else if (compareBuf(token, "rtsp", 4))
            {
                token = strtok(NULL, SPLIT_CHAR);
                key = charArrayToInt(token);

                if (key < 0 || key > 1)
                {
                    g_print("key %d not supported\n", key);
                    return -1;
                }

                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].setkeyframe(key);
            }
            else
                g_print("wrong cmd!\n");
        }
        else if (compareBuf(token, "dbg", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (compareBuf(token, "rtsp", 4))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                        rtspServerBin[i].setTimeStampDebug();

                if(cmdArg.audio_en) rtspServerBin[4].setTimeStampDebug();
            }
            else if (compareBuf(token, "rec", 3))
            {

            }
            else if (compareBuf(token, "cap", 3))
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                        captureBin[i].setTimeStampDebug();
            }
        }
        else
            g_print("wrong cmd!\n");
    } // set
    else if (compareBuf(token, "start", 5))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            key = charArrayToInt(token);
            
            if (key < 0 || key > 3)
            {
                g_print("channel %d not supported\n", i);
                return -1;
            }

            token = strtok(NULL, SPLIT_CHAR);
            key1 = charArrayToInt(token);

            if(key1 < 0 || key1 > 2)
            {
                g_print("cap mode %d not supported\n", key1);
                return -1;
            }

            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                key2 = 0;
            }
            else
            {
                key2 = charArrayToInt(token);
                if(key2 < 0)
                {
                    g_print("cap max cnt %d not supported\n", key2);
                    return -1;
                }
            }

            if (key1 == 0)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) == TRUE)
                        {
                            if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()))
                            {
                                g_print("ch%d rec unlink ok!\n", i);
                            }
                            else
                                g_print("ch%d rec unlink err!\n", i);
                        }
                        else
                            g_print("ch%d rec already unlinked!\n", i);
                    }
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) == TRUE)
                        {
                            if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()))
                            {
                                g_print("ch%d rtsp unlink ok!\n", i);
                            }
                            else
                                g_print("ch%d rtsp unlink err!\n", i);
                        }
                        else
                            g_print("ch%d rtsp already unlinked!\n", i);
                    }
                }
            }

            if (cmdArg.cam_en[key] && cmdArg.stream_en[STREAM_CAP])
            {
                if(key1 == 2)
                {
                    if(!captureBin[key].init(key, cmdArg.crop_en[key/2]))
                        g_print("ch%d captrueBin init failed\n",key);

                    if(captureBin[key].addBinToPipe(pipeline)) g_print("ch%d capture bin add\n", key);
                    else g_print("ch%d capture bin add error\n", key);
                    // g_usleep(10000);

                    if (gst_pad_is_linked(videoBin[key / 2].getBinCaptureSrcPad(key)) != TRUE)
                    {
                        //g_print("ch%d capture not linked\n", key);
                        if (gst_pad_link(videoBin[key / 2].getBinCaptureSrcPad(key), captureBin[key].getBinSinkPad()) != GST_PAD_LINK_OK)
                        {
                            g_print("ch%d capture link error!\n", key);
                            // captureBin[i].removeBinToPipe(pipeline);
                            return -1;
                        }
                        else
                        {
                            g_print("ch%d capture link ok!\n", key);
                            // gst_element_sync_state_with_parent(captureBin[i].be.bin);
                            // gst_element_set_state(pipeline, GST_STATE_PLAYING);
                        }
                    }
                    else
                    {
                        g_print("ch%d capture already linked!\n", key);
                        // gst_element_sync_state_with_parent(captureBin[i].be.bin);
                        // gst_element_set_state(pipeline, GST_STATE_PLAYING);
                    }

                    for(i=0; i<500; i++)
                    {
                        g_print("pipeline playing\n");
                        gst_element_set_state(pipeline, GST_STATE_PLAYING);
                        //g_print("ch%d captrue state sync\n", key);
                        //gst_element_sync_state_with_parent(captureBin[key].be.bin);
                        //g_usleep(1000);
                        state = captureBin[key].getState();
                        if (state == GST_STATE_PLAYING)
                            break;
                        else
                            g_print("state : %d\n", state);

                        g_usleep(1000);
                    }
                }
                
                captureBin[key].setFilePath(NULL);

                if(key1 == 0) {
                    key2 = cmdArg.fps[STREAM_CAP][key]*60;
                } else if(key1 == 1) {
                    if(key2 == 0) key2 = cmdArg.captureMaxCnt;
                } else if(key1 == 2) {
                    if(key2 == 0) key2 = cmdArg.captureMaxCnt;
                } 

                g_print("ch:%d, fps:%d, mode : %d, max_cnt : %d\n", key, cmdArg.fps[STREAM_CAP][key], key1, key2);
                captureBin[key].startCapture(key2);
                
                // captureBin[i].setState(GST_STATE_READY);
                if (key1 == 2)
                {
#if 0
                    //g_usleep(100000);
                    time = (1000/cmdArg.fps[STREAM_CAP][i])*(key2+1);
                    g_print("sleep time : %dmsec\n", time);
                    g_usleep(time*1000);
#endif
                    for(i=0; i<500; i++)
                    {
                        if(captureBin[key].getCaptureCnt() >= key2)
                            break;
                        
                        g_usleep(10000);
                    }
                    
                    __LOG(LOG_NOTICE, "[GST][%s:%d] capture end", _FILE_, __LINE__);

                    if (gst_pad_unlink(videoBin[key / 2].getBinCaptureSrcPad(key), captureBin[key].getBinSinkPad()))
                    {
                        g_print("ch%d capture unlink ok!\n", key);
                        state = captureBin[key].getState();
                        g_print("state : %d\n", state);
                        for(i=0; i<500; i++) 
                        {
                            captureBin[key].setState(GST_STATE_NULL);
                            state = captureBin[key].getState();
                            g_print("state : %d\n", state);
                            if(state == GST_STATE_NULL) {
                                if(captureBin[key].removeBinToPipe(pipeline)) g_print("ch%d capture bin remove\n", key);
                                else g_print("ch%d capture bin remove error\n", key);

                                break;
                            }
                            g_usleep(10000);
                        }
                    }
                    else
                        g_print("ch%d capture unlink err!\n", key);

                    // captureBin[i].removeBinToPipe(pipeline);
                }
            }
        }
        else if (compareBuf(token, "rec", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (token == NULL)
            {
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                for (i = 0; i < MAX_CHANNEL; i++)
                {

                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        gst_element_sync_state_with_parent(recordBin[i].re.bin);

                        while (1)
                        {
                            state = recordBin[i].getState();
                            if (state == GST_STATE_PLAYING)
                                break;
                            else
                                g_print("ch%d rec state : %d\n", i, state);

                            g_usleep(1000);
                        }
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                {
                    gst_element_sync_state_with_parent(recordBin[i].re.bin);

                    while (1)
                    {
                        state = recordBin[i].getState();
                        if (state == GST_STATE_PLAYING)
                            break;
                        else
                            g_print("ch%d rec state : %d\n", i, state);

                        g_usleep(1000);
                    }
                }
            }
        }
        else if (compareBuf(token, "rtsp", 4))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if (token == NULL)
            {
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        gst_element_sync_state_with_parent(rtspServerBin[i].re.bin);

                        while (1)
                        {
                            state = rtspServerBin[i].getState();
                            if (state == GST_STATE_PLAYING)
                                break;
                            else
                                g_print("ch%d rtsp state : %d\n", i, state);

                            g_usleep(1000);
                        }
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                {
                    gst_element_sync_state_with_parent(rtspServerBin[i].re.bin);

                    while (1)
                    {
                        state = rtspServerBin[i].getState();
                        if (state == GST_STATE_PLAYING)
                            break;
                        else
                            g_print("ch%d rtsp state : %d\n", i, state);

                        g_usleep(1000);
                    }
                }
            }
        }
        else if (compareBuf(token, "split", 5))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                g_print("split now\n");
                for (i = 0; i < MAX_CHANNEL; i++) if (muxSinkBin[i].getBinVideoSinkPad()) muxSinkBin[i].splitNow(NULL, FALSE);
            }
            else
            {
                key = charArrayToInt(token);

                if (key < 0 || key > 59)
                {
                        g_print("set_sec %d not supported\n", key);
                        return -1;
                }
                g_print("split set_sec : %d\n", key);
                cmdArg.split_sec = key;
            }
        }
        else
            g_print("wrong cmd!\n");
    }   //start
    else if (compareBuf(token, "link", 4))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                //gst_element_set_state(pipeline, GST_STATE_PAUSED);
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) != TRUE)
                        {
                            if (gst_pad_link(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                            {
                                g_print("ch%d capture link error!\n", i);
                            }
                            else
                            {
                                captureBin[i].stopCapture();
                                g_print("ch%d capture link ok!\n", i);
                            }
                        }
                        else
                            g_print("ch%d capture already link!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                {
                    if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) != TRUE)
                    {
                        if (gst_pad_link(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                        {
                            g_print("ch%d capture link error!\n", i);
                        }
                        else
                        {
                            captureBin[i].stopCapture();
                            g_print("ch%d capture link ok!\n", i);
                        }
                    }
                    else
                        g_print("ch%d capture already link!\n", i);
                }
            }

            //gst_element_sync_state_with_parent
            //gst_element_set_state(pipeline, GST_STATE_PLAYING);

            //for (i = 0; i < MAX_CHANNEL; i++) if (captureBin[i].getBinSinkPad()) captureBin[i].stopCapture();
        }
        else if(compareBuf(token, "rec", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) != TRUE)
                        {
                            if (gst_pad_link(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                            {
                                g_print("ch%d rec link error!\n", i);
                            }
                            else
                                g_print("ch%d rec link ok!\n", i);
                        }
                        else
                            g_print("ch%d rec already link!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                {
                    if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) != TRUE)
                    {
                        if (gst_pad_link(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                        {
                            g_print("ch%d rec link error!\n", i);
                        }
                        else
                            g_print("ch%d rec link ok!\n", i);
                    }
                    else
                        g_print("ch%d rec already link!\n", i);
                }
            }
        }
        else if(compareBuf(token, "rtsp", 4))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) != TRUE)
                        {
                            if (gst_pad_link(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                            {
                                g_print("ch%d rtsp link error!\n", i);
                            }
                            else
                                g_print("ch%d rtsp link ok!\n", i);
                        }
                        else
                            g_print("ch%d rtsp already link!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                {
                    if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) != TRUE)
                    {
                        if (gst_pad_link(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                        {
                            g_print("ch%d rtsp link error!\n", i);
                        }
                        else
                            g_print("ch%d rtsp link ok!\n", i);
                    }
                    else
                        g_print("ch%d rtsp already link!\n", i);
                }
            }
        }
        else
            g_print("wrong cmd!\n");
    }   //link
    else if (compareBuf(token, "unlink", 6))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                //gst_element_set_state(pipeline, GST_STATE_PAUSED);
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) == TRUE)
                        {
                            if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()))
                            {
                                g_print("ch%d capture unlink ok!\n", i);
                            }
                            else
                            {
                                captureBin[i].stopCapture();
                                g_print("ch%d capture unlink err!\n", i);
                            }
                        }
                        else
                            g_print("ch%d capture already unlink!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                {
                    if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) == TRUE)
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()))
                        {
                            g_print("ch%d capture unlink ok!\n", i);
                        }
                        else
                        {
                            captureBin[i].stopCapture();
                            g_print("ch%d capture unlink err!\n", i);
                        }
                    }
                    else
                        g_print("ch%d capture already unlink!\n", i);
                }
            }

            //gst_element_sync_state_with_parent
            //gst_element_set_state(pipeline, GST_STATE_PLAYING);

            //for (i = 0; i < MAX_CHANNEL; i++) if (captureBin[i].getBinSinkPad()) captureBin[i].stopCapture();
        }
        else if(compareBuf(token, "rec", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        if (1)  //(gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) == TRUE)
                        {
                            if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()))
                            {
                                g_print("ch%d rec unlink ok!\n", i);
                            }
                            else
                                g_print("ch%d rec unlink err!\n", i);
                        }
                        else
                            g_print("ch%d rec already unlink!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                {
                    if (1)
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()))
                        {
                            g_print("ch%d rec unlink ok!\n", i);
                        }
                        else
                            g_print("ch%d rec unlink err!\n", i);
                    }
                    else
                        g_print("ch%d rec already unlink!\n", i);
                }
            }
        }
        else if(compareBuf(token, "rtsp", 4))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) == TRUE)
                        {
                            if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()))
                            {
                                g_print("ch%d rtsp unlink ok!\n", i);
                            }
                            else
                                g_print("ch%d rtsp unlink err!\n", i);
                        }
                        else
                            g_print("ch%d rtsp already unlink!\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                {
                    if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) == TRUE)
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()))
                        {
                            g_print("ch%d rtsp unlink ok!\n", i);
                        }
                        else
                            g_print("ch%d rtsp unlink err!\n", i);
                    }
                    else
                        g_print("ch%d rtsp already unlink!\n", i);
                }
            }
        }
        else
            g_print("wrong cmd!\n");
    }   //unlink
    else if (compareBuf(token, "add", 3))
    {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        if(captureBin[i].addBinToPipe(pipeline)) g_print("ch%d capture bin add\n", i);
                        else g_print("ch%d capture bin add error\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP]) {
                    //g_print("ch%d caputre bin add\n", i);
                    if(captureBin[i].addBinToPipe(pipeline)) g_print("ch%d capture bin add\n", i);
                    else g_print("ch%d capture bin add error\n", i);
                }
            }
        }
        else if (compareBuf(token, "rec", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        if(recordBin[i].addBinToPipe(pipeline)) g_print("ch%d record bin add\n", i);
                        else g_print("ch%d record bin add error\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC]) {
                    //g_print("ch%d caputre bin add\n", i);
                    if(recordBin[i].addBinToPipe(pipeline)) g_print("ch%d record bin add\n", i);
                    else g_print("ch%d record bin add error\n", i);
                }
            }
        }
        else if (compareBuf(token, "rtsp", 4))
        {
            token = strtok(NULL, SPLIT_CHAR);
            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                {
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        if(rtspServerBin[i].addBinToPipe(pipeline)) g_print("ch%d rtsp bin add\n", i);
                        else g_print("ch%d rtsp bin add error\n", i);
                    }
                }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP]) {
                    //g_print("ch%d caputre bin add\n", i);
                    if(rtspServerBin[i].addBinToPipe(pipeline)) g_print("ch%d rtsp bin add\n", i);
                    else g_print("ch%d rtsp bin add error\n", i);
                }
            }
        } 
        else
            g_print("wrong cmd!\n");
    }   //add
    else if (compareBuf(token, "rm", 2))     //else if (!strncmp(buffer, "rm", 2))
    {
        token = strtok(NULL, SPLIT_CHAR);

        if (compareBuf(token, "cap", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_CAP])
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != TRUE)
                        {
                            g_print("ch%d capture unlink error!\n", i);
                        }
                        else
                            g_print("ch%d capture unlink ok!\n", i);

                        if(captureBin[i].removeBinToPipe(pipeline)) g_print("ch%d capture bin remove\n", i);
                        else g_print("ch%d capture bin remove error\n", i);
                    }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != TRUE)
                {
                    g_print("ch%d capture unlink error!\n", i);
                }
                else
                {
                    g_print("ch%d capture unlink ok!\n", i);
                    if(captureBin[i].removeBinToPipe(pipeline)) g_print("ch%d capture bin remove\n", i);
                    else g_print("ch%d capture bin remove error\n", i);
                }
            }
        }
        else if (compareBuf(token, "rec", 3))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC])
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()) != TRUE)
                        {
                            g_print("ch%d recordBin unlink error!\n", i);
                        }
                        else
                        {
                            g_print("ch%d recordBin unlink ok!\n", i);
                            if(recordBin[i].removeBinToPipe(pipeline)) g_print("ch%d record bin remove\n", i);
                            else g_print("ch%d record bin remove error\n", i);
                        }
                    }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()) != TRUE)
                {
                    g_print("ch%d recordBin unlink error!\n", i);
                }
                else
                {
                    g_print("ch%d recordBin unlink ok!\n", i);
                    if(recordBin[i].removeBinToPipe(pipeline)) g_print("ch%d record bin remove\n", i);
                    else g_print("ch%d record bin remove error\n", i);
                }
            }
        }
        else if (compareBuf(token, "rtsp", 4))
        {
            token = strtok(NULL, SPLIT_CHAR);

            if(token == NULL)
            {
                for (i = 0; i < MAX_CHANNEL; i++)
                    if (cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP])
                    {
                        if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()) != TRUE)
                        {
                            g_print("ch%d rtspServerBin unlink error!\n", i);
                        }
                        else
                        {
                            g_print("ch%d rtspServerBin unlink ok!\n", i);
                            if(rtspServerBin[i].removeBinToPipe(pipeline)) g_print("ch%d rtspServerBin remove\n", i);
                            else g_print("ch%d record bin remove error\n", i);
                        }
                    }
            }
            else
            {
                i = charArrayToInt(token);

                if (i < 0 || i > 3)
                {
                    g_print("channel %d not supported\n", i);
                    return -1;
                }

                if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()) != TRUE)
                {
                    g_print("ch%d rtspServerBin unlink error!\n", i);
                }
                else
                {
                    g_print("ch%d rtspServerBin unlink ok!\n", i);
                    if(rtspServerBin[i].removeBinToPipe(pipeline)) g_print("ch%d rtspServerBin remove\n", i);
                    else g_print("ch%d record bin remove error\n", i);
                }
            }
        }
        else
            g_print("wrong cmd!\n");
    }   //rm
    else
    {
        g_print("wrong cmd!\n");
    }
#endif

    return 0;
}
