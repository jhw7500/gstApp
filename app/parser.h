/*
 *
 * Cantops aes.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _PARSER_H_
#define _PARSER_H_

#include "util.h"
#include <json-c/json.h>

#define JSON_NAME_PREFIX  "edgeconf_"
#define JSON_NAME_SUFFIX  ".json"

#define DEFAULT_ENABLE_PATH_01	"/sys/bus/i2c/devices/i2c-2/2-0048/enable"
#define DEFAULT_ENABLE_PATH_23	"/sys/bus/i2c/devices/i2c-1/1-0048/enable"

#define DEFAULT_ROTATE_PATH_01	"/sys/bus/i2c/devices/i2c-2/2-0048/rotate"
#define DEFAULT_ROTATE_PATH_23	"/sys/bus/i2c/devices/i2c-1/1-0048/rotate"

#define DEFAULT_JSON_PATH   "/root/shared_v"
#define DEFAULT_JSON_HEADER "VHL_CAM"
#define DEFAULT_RECORD_BITRATE  4096
#define DEFAULT_RTSP_BITRATE    1024
#define DEFAULT_MAIN_FPS        15
#define DEFAULT_RECORD_FPS      15
#define DEFAULT_RTSP_FPS        15
#define DEFAULT_CAPTURE_FPS     15
#define DEFAULT_GOP_SIZE        15
#define DEFUALT_DURATION        1
#define DEFAULT_DBG_LEVEL       4
#define DEFAULT_LOG_LEVEL       5
#define DEFAULT_CH_ENABLE       0x00
#define DEFAULT_CH_ROTATE       0x00
#define DEFAULT_WIDTH           1920
#define DEFAULT_HEIGHT          1080

#define DEFAULT_AWB             "auto"
#define DEFAULT_LSC             0x3fff
#define DEFAULT_AE_GAIN         0x100
#define DEFAULT_ISO             100
#define DEFAULT_EXP_TIME        10000

#define DEFAULT_RTSP_PORT   "8554"
#define DEFAULT_RTSP_ID     "user"
#define DEFAULT_RTSP_PASSWD "user"

#define DEFAULT_TCP_PORT    8555
#define DEFAULT_IPC_MID     0x65

#define DEFAULT_PLAY_DELAY  5
#define DEFAULT_CAPTURE_MAX_CNT 3
#define DEFAULT_SPLIT_SEC           0
#define DEFAULT_SPLIT_DIFF_MSEC     200
#define DEFAULT_SPLIT_MAX_MSEC      5000
#define DEFAULT_SPLIT_AUDIO_MIN_MSEC      59000

#define DEFAULT_START_VIDEO_TIME_PATH   "/tmp/start_video_time"
#define DEFAULT_DOT_PATH    "/tmp"
#define JHW_TESTx
#ifdef JHW_TEST
#define DEFAULT_MOUNT_PATH   "/home/user/jhw"
#define DEFAULT_CAPTURE_PATH "/home/user/jhw"
#else
#define DEFAULT_MOUNT_PATH   "/mnt/sd_cam"
#define DEFAULT_CAPTURE_PATH   "/mnt/sd_cam/capture"
#endif

#define MAX_FPS_HD  240
#define MAX_FPS_FHD 180

#define CFI_DATA_LEN        50
#define CFI_VERSION         0x300
#define CFI_CAPTURE_CMD_ID  0x300

#pragma pack(push, 1)
union TCfiData {
  guint8 byte[CFI_DATA_LEN];
  struct THeader {
    guint16 len;
	guint16 ver;
    guint8 sid[6];
    guint16 cmd_id;
    guint16 tx_id;
    guint16 reserved;
    guint16 cap_cnt;
    guint8 prefix[32];
  } data;
};
#pragma pack(pop)

class ParserClass
{
public :
	static ParserClass* getInstance() ;
    ParserClass();
    ~ParserClass();
	gint init() ;
    gint cmd_parser(gchar* buffer, gint len, gpointer data);
    gint cfi_parser(gchar* buffer, gint len, gpointer data);
    gint json_parser(const gchar *path, const gchar *header);
    gint arg_parser(int *argc, char **argv[]);
    gint check_arg();
    void init_arg(gchar *argv);
    static gint json_object_get_value(json_object *hobj, const gchar *name, gpointer data);
    static json_object *json_find_obj (json_object * jobj, char *find_key);
    
private :
	
public :
	gboolean m_flagDestroy;
    CmdArg arg;
	
private :

};

#endif