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

#ifndef _UTIL_H_
#define _UTIL_H_

#include <syslog.h>
//#include <cstdio>
#include <gst/gst.h>
#include <stdio.h>
#include "util.h"

#define DEBUG_TIMESTAMP
#define MAX_CHANNEL 4
#define MAX_VIDEO_SRC 2

#define DEFAULT_RECORD_BITRATE  4096
#define DEFAULT_RTSP_BITRATE    1024
#define DEFAULT_MAIN_FPS 15
#define DEFAULT_RECORD_FPS  15
#define DEFAULT_RTSP_FPS 15
#define DEFUALT_DURATION    1
#define DEFAULT_DBG_LEVEL   5
#define DEFAULT_LOG_LEVEL   6

#define DEFAULT_RTSP_PORT   "8554"
#define DEFAULT_RTSP_ID     "semes"
#define DEFAULT_RTSP_PASSWD "semes"

#define QUEUE_TYPE          "queue"
#define LEAKY_NONE          0
#define LEAKY_UPSTREAM      1
#define LEAKY_DOWNSTREAM    2

#define DEFAULT_PLAY_DELAY  0
#define DEFAULT_CAPTURE_MAX_CNT 3
#define DEFAULT_OVERLAY_FONT "Times New Roman Italic, 12"

#define JHW_TESTx
#ifdef JHW_TEST
#define MOUNT_PATH   "/home/user/jhw"
#define CAPTURE_PATH "/home/user/jhw"
#else
#define DEFAULT_MOUNT_PATH   "/mnt/sd_cam"
#define DEFAULT_CAPTURE_PATH   "/mnt/sd_cam/capture"
#endif
#define DEFULAT_DOT_PATH    "/tmp"

typedef enum
{
    CSI_1 = 0,
    CSI_2 = 1
} CsiNum;

typedef enum
{
    STREAM_RECORD = 0,
    STREAM_RTSP = 1
} StreamMode;

typedef enum
{
    CH0 = 0,
    CH1 = 1,
    CH2 = 2,
    CH3 = 3
} ChannelNum;

typedef enum
{
    CROP_L = 0,
    CROP_R = 1
} CropDir;

typedef enum
{
    ResFHD = 0,
    ResHD = 1
} ResMode;

typedef enum
{
    NORMAL_MODE = 0,
    TEST_MODE = 1
} gstMode;

typedef struct _Resolution
{
    guint16 width;
    guint16 height;
} Resolution;

typedef struct _CmdArg
{
    gstMode mode;
    gint log_level;
    gint dbg_level;
    const gchar *dotDir;
    const gchar *mntDir;
    gchar *ohtName;
    gint ch_enable;
    ResMode resMode;
    gint main_fps;
    gint rec_fps;
    gint rtsp_fps;
    gint rec_bitrate;
    gint rtsp_bitrate;
    gint play_delay;
    gboolean fault;
    gint duration;
    gboolean rtsp_en;
    gboolean rec_en;
    gboolean capture_en;
    gboolean audio_en;
    gchar *appname;
    guint8 captureMaxCnt;
    gboolean input_en;
    const gchar *rtsp_port;
    const gchar *captureDir;
    const gchar *rtsp_id;
    const gchar *rtsp_passwd;
    gboolean overlay_en;
    Resolution res[2] = {{1920,1080}, {1280,720}};
} CmdArg;

//extern gchar *program_name;
extern GstElement *pipeline;
extern GMainLoop *loop;
extern volatile sig_atomic_t is_interrupted;
extern CmdArg cmdArg;
extern gboolean is_live;

void log_once(gint opt, const gchar *message);
extern guint charArrayToInt(gchar *arr);
extern gboolean compareBuf(guint8 *cmp1, guint8 *cmp2, guint8 len);
extern gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data);
extern GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
extern gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer);
void mylog(gint opt, const gchar* _szfmt, ... );
gint cmd_parser(int *argc, char **argv[], gpointer data);
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

//extern void attachInterruptHandlers();

#endif