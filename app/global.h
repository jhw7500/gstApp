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


#ifndef _GLOBAL_H_
#define _GLOBAL_H_

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

//extern gchar *program_name;
extern GstElement *pipeline;
extern GMainLoop *loop;
extern gchar *ohtName;
extern volatile sig_atomic_t is_interrupted;

extern gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data);

#endif