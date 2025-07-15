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

#define GST_API_VERSION "1.0"

#define CHANNEL_EACH_CROP
#define DEBUG_TIMESTAMP
#define MAX_CHANNEL 4
#define MAX_VIDEO_SRC 2
#define MAX_MODE    3

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
    STREAM_RTSP = 1,
    STREAM_CAP = 2
} StreamMode;

typedef enum
{
    CROP_L = 0,
    CROP_R = 1
} CropDir;

typedef enum
{
    RES_NONE = 0,
    RES_HD = 1,
    RES_FHD = 2
} ResMode;

typedef enum
{
    MODE_NORMAL = 0,
    MODE_TEST = 1
} LevelMode;

typedef enum
{
    IO_AUTO = 0,
    IO_RW_ = 1,
    IO_MMAP =2,
    IO_USERPTR =3,
    IO_DMABUF = 4,
    IO_DMABUF_IMPORT = 5
} IoMode;

typedef enum
{
    ROTATE_NONE = 0,
    ROTATE_90 = 1,
    ROTATE_180 =2,
    ROTATE_270 =3,
    ROTATE_HORIZONTAL_FLIP = 4,
    ROTATE_VERTICAL_FLIP = 5
} Rotation;

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
    void* arg5;
} ThreadArgs;

typedef struct {
    gboolean enable;
    gboolean vflip;
    gboolean hflip;
    gint bps[2];
    gint gop[2];
    gboolean ae_on;
    guint ae_gain;
    guint lsc;
    guint iso;
    guint32 exp_time;
    const gchar *awb;
} CamConfig;

typedef struct {
    guint32 exp_time;
} I2cConfig;

typedef struct _CmdArg
{
    LevelMode levelMode;
    IoMode ioMode;
    I2cConfig i2cConfig[MAX_VIDEO_SRC];
    CamConfig camConfig[MAX_CHANNEL];
    gboolean hflip[MAX_CHANNEL];
    gboolean vflip[MAX_CHANNEL];
    gboolean cam_en[MAX_CHANNEL];
    gint fps[MAX_MODE][MAX_CHANNEL];
    gint bps[MAX_MODE][MAX_CHANNEL];
    gboolean stream_en[MAX_MODE];
    gint gop[MAX_MODE][MAX_CHANNEL];
    gint log_level;
    gint dbg_level;
    const gchar *dotDir;
    const gchar *mntDir;
    const gchar *ohtName;
    const gchar *json_file;
    guint8 ch_enable;
    guint16 ch_rotate;
    ResMode resMode;
    gint main_fps[MAX_VIDEO_SRC];
    gint play_delay;
    gboolean fault;
    gint duration;
    gboolean cap_encoder_en;
    gboolean cap_always;
    gboolean cap_res_en;
    gboolean tcp_en;
    gint tcp_port;
    gboolean audio_en;
    const gchar *appname;
    gint captureMaxCnt;
    gint split_diff_msec;
    gint split_max_msec;
    gint split_audio_min_msec;
    gint split_sec;
    gboolean input_en;
    const gchar *rtsp_port;
    const gchar *captureDir;
    const gchar *rtsp_id;
    const gchar *rtsp_passwd;
    gboolean overlay_en;
    gint width;
    gint height;
    gboolean crop_en[2];
    gboolean ipc_en;
    gint ipc_mid;
    gint cap_delay;
    gint cap_timeout;
    const gchar *muxer;
} CmdArg;

#pragma pack(pop)

//extern gchar *program_name;
extern GstElement *pipeline;
extern GstElement *pipeline2;
extern GMainLoop *loop;
extern volatile sig_atomic_t is_interrupted;
extern CmdArg cmdArg;
extern gboolean is_live;

void fault_setup (void);
void addSignalHandler();
void attachInterruptHandlers();
void removeSignalHandler();
void mylog(gint opt, const gchar* _szfmt, ... );
void log_once(gint opt, const gchar *message);
gint charArrayToInt(gchar *arr);
gboolean compareBuf(const gchar *cmp1, const gchar *cmp2, guint8 len);
GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer);
gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix);
void print_tag(const GstTagList * list, const gchar * tag, gpointer unused);
void makeDir(const char* path);
void convert_data_to_hex(const char *data, int len, char *buffer, int buffer_size);

#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

#endif
