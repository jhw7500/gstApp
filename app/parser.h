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

#define DEFAULT_ROTATE_PATH_01	"/sys/bus/i2c/devices/i2c-2/2-0048/rotate"
#define DEFAULT_ROTATE_PATH_23	"/sys/bus/i2c/devices/i2c-1/1-0048/rotate"

#define DEFAULT_PATH_JSON   "/root/shared_v"
#define DEFAULT_RECORD_BITRATE  4096
#define DEFAULT_RTSP_BITRATE    1024
#define DEFAULT_MAIN_FPS    15
#define DEFAULT_RECORD_FPS  15
#define DEFAULT_RTSP_FPS    15
#define DEFAULT_GOP_SIZE    15
#define DEFUALT_DURATION    1
#define DEFAULT_DBG_LEVEL   5
#define DEFAULT_LOG_LEVEL   6
#define DEFAULT_CH_ENABLE   0x0f
#define DEFAULT_CH_ROTATE   0x00
#define DEFAULT_WIDTH       1920
#define DEFAULT_HEIGHT      1080

#define DEFAULT_RTSP_PORT   "8554"
#define DEFAULT_RTSP_ID     "user"
#define DEFAULT_RTSP_PASSWD "user"

#define DEFAULT_PLAY_DELAY  3
#define DEFAULT_CAPTURE_MAX_CNT 3
#define DEFAULT_SPLIT_MARGIN_MSEC    100
#define DEFAULT_SPLIT_MAX_MSEC      1500

#define DEFULAT_DOT_PATH    "/tmp"
#define JHW_TESTx
#ifdef JHW_TEST
#define DEFAULT_MOUNT_PATH   "/home/user/jhw"
#define DEFAULT_CAPTURE_PATH "/home/user/jhw"
#else
#define DEFAULT_MOUNT_PATH   "/mnt/sd_cam"
#define DEFAULT_CAPTURE_PATH   "/mnt/sd_cam/capture"
#endif

gboolean bus_message_parse(GstBus *bus, GstMessage *message, gpointer data);

class ParserClass
{
public :
	static ParserClass* getInstance() ;
    ParserClass();
    ~ParserClass();
	gint init() ;
	gint destroy() ;
    gint cmd_parser(gchar *path, gpointer data);
    gint json_parser(const gchar *path);
    gint arg_parser(int *argc, char **argv[], gpointer data);
    void print_option();
    void init_arg();
    
private :
	
public :
	gboolean m_flagDestroy;
	
private :

};

#endif