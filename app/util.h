/*
 *
 * Cantops util.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <syslog.h>
//#include <cstdio>
#include <gst/gst.h>
#include <stdio.h>
#include "util.h"

#define CHANNEL_EACH_CROP
#define DEBUG_TIMESTAMP
#define MAX_CHANNEL 4
#define MAX_VIDEO_SRC 2

#define DEFAULT_OVERLAY_FONT "Times New Roman Italic, 12"

#define QUEUE_TYPE          "queue"
#define LEAKY_NONE          0
#define LEAKY_UPSTREAM      1
#define LEAKY_DOWNSTREAM    2

typedef enum
{
    CSI_1 = 0,
    CSI_2 = 1
} CsiNum;

typedef enum
{
    STREAM_REC = 0,
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
} TestMode;

typedef enum
{
    AUTO_MODE = 0,
    RW_MODE = 1,
    MMAP_MODE =2,
    USERPTR_MODE =3,
    DMABUF_MODE = 4,
    DMABUF_IMPORT_MODE = 5
} IoMode;

typedef enum
{
    NONE_MODE = 0,
    ROTATE_90_MODE = 1,
    ROTATE_180_MODE =2,
    ROTATE_270_MODE =3,
    HORIZONTAL_FLIP_MODE = 4,
    VERTICAL_FLIP_MODE = 5
} RotationMode;

typedef struct _Resolution
{
    guint16 width;
    guint16 height;
} Resolution;

#pragma pack(push, 1)

typedef struct {
    void* arg0;
    void* arg1;
    void* arg2;
    void* arg3;
    void* arg4;
} ThreadArgs;

typedef struct _CmdArg
{
    TestMode testMode;
    IoMode ioMode;
    gboolean cam_rotate[MAX_CHANNEL];
    gboolean cam_en[MAX_CHANNEL];
    gint fps[MAX_CHANNEL/2];
    gint bps[MAX_CHANNEL/2];
    gboolean stream_en[MAX_CHANNEL/2];
    gint gop[MAX_CHANNEL/2];
    gint log_level;
    gint dbg_level;
    const gchar *dotDir;
    const gchar *mntDir;
    const gchar *ohtName;
    guint8 ch_enable;
    guint8 ch_rotate;
    ResMode resMode;
    gint main_fps;
    gint play_delay;
    gboolean fault;
    gint duration;
    gboolean capture_en;
    gboolean audio_en;
    const gchar *appname;
    gint captureMaxCnt;
    gint split_margin_msec;
    gint split_max_msec;
    gboolean input_en;
    const gchar *rtsp_port;
    const gchar *captureDir;
    const gchar *rtsp_id;
    const gchar *rtsp_passwd;
    gboolean overlay_en;
    gint width;
    gint height;
} CmdArg;

#pragma pack(pop)

//extern gchar *program_name;
extern GstElement *pipeline;
extern GMainLoop *loop;
extern volatile sig_atomic_t is_interrupted;
extern CmdArg cmdArg;
extern gboolean is_live;
extern const char *test0;
extern const char *test1;
extern const char *test2;

void mylog(gint opt, const gchar* _szfmt, ... );
void log_once(gint opt, const gchar *message);
guint charArrayToInt(gchar *arr);
gboolean compareBuf(guint8 *cmp1, guint8 *cmp2, guint8 len);
GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer);
gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix);
void print_tag(const GstTagList * list, const gchar * tag, gpointer unused);

#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

#endif