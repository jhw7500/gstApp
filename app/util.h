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

#define _WIDTH   1920
#define _HEIGHT  1080
#define MAIN_BITRATE    RECORD_BITRATE
#define RECORD_BITRATE  4096
#define RTSP_BITRATE    1024
#define MAIN_FPS RECORD_FPS
#define RECORD_FPS  15
#define RTSP_FPS 15
#define DEFUALT_DURATION    1

#define LEAKY_NONE          0
#define LEAKY_UPSTREAM      1
#define LEAKY_DOWNSTREAM    2

#define MAX_CHANNEL 4
#define MAX_VIDEO_SRC 2

#define JHW_TESTx
#ifdef JHW_TEST
#define FILE_PATH   ""
#else
#define FILE_PATH   "/mnt/sd_cam/"
#endif

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

typedef struct _Resolution
{
    guint16 width;
    guint16 height;
} Resolution;

typedef struct _CmdArg
{
    gint debug_level;
    gchar *saveDot;
    gchar *saveDir;
    gchar *ohtName;
    gint ch_enable;
    ResMode resMode;
    gint main_fps;
    gint rec_fps;
    gint rtsp_fps;
    gint rec_bitrate;
    gint rtsp_bitrate;
    gint play_delay;
    gint no_fault;
    gint duration;
    Resolution res[2];
} CmdArg;

//extern gchar *program_name;
extern GstElement *pipeline;
extern GMainLoop *loop;
extern volatile sig_atomic_t is_interrupted;
extern CmdArg cmdArg;
extern gboolean is_live;

extern gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data);
void mylog( int opt, const char* _szfmt, ... );
gint cmd_parser(int *argc, char **argv[], gpointer data);
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

//extern void attachInterruptHandlers();

#endif