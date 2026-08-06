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
#define SD_MOUNT_FLAG   "/dev/shm/sd_mount_flag"

#define DEFAULT_OVERLAY_FONT "Times New Roman Italic, 12"

#define QUEUE_TYPE          "queue"
#define LEAKY_NONE          0
#define LEAKY_UPSTREAM      1
#define LEAKY_DOWNSTREAM    2

#define ENC_H264            "h264"
#define ENC_H265            "h265"

#define FALLBACKDIR     "/dev/shm"

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
    /* VPU encoder tuning, per stream ([STREAM_REC], [STREAM_RTSP]).
     * H.264 profile: 9=Baseline 10=Main 11=High 12=High10
     * H.265 profile: 0=Main 1=Main Still Picture 2=Main10
     * quant  : initial QP, -1 = auto (rate control keeps running either way)
     * qp_min / qp_max: 0..51, 0 means "unset" to the VPU wrapper (it only
     *          honours values > 0), so 0 leaves the hardware default in place.
     * profile is installed on i.MX8MP only, qp_min/qp_max on i.MX8MP and
     * i.MX8MM, so encoderBin checks the property exists before setting it. */
    gint profile[2];
    gint quant[2];
    gint qp_min[2];
    gint qp_max[2];
    gboolean ae_on;
    guint ae_gain;
    guint lsc;
    guint iso;
    guint32 exp_time;
    const gchar *awb;
    /* LED flash (max9296 driver: mcp4018_power_chX + mcp4018_wiper_chX + led_flash_chX) */
    gboolean led_flash_enable;  /* gates MCP4018 VCC (MFP4 GPIO) + AR0234 0x3270 bit8 */
    guint    led_flash_wiper;   /* 0..127 MCP4018 wiper step */
    guint    led_flash_delay;   /* 0..255 AR0234 0x3270 bit7:0 DELAY */
} CamConfig;

typedef struct {
    gboolean res_en;
    gboolean rtsp_en;
    gboolean record_en;
    gint maxCnt;
    gboolean padding;
    gint delay;
    gint timeout;
    gint quality;
    gint queue_size;
    const gchar *dir;     // absolute path for capture output directory (optional)
    const gchar *path;
    const gchar *encoder;
    gint instant;        // [instant-snapshot] 0=off, 1=ref-hold (light), 2=deep-copy (safe); default 0
} CaptureConfig;

typedef struct _CmdArg
{
    LevelMode levelMode;
    IoMode ioMode;
    CamConfig cam[MAX_CHANNEL];
    gint fps[MAX_MODE][MAX_CHANNEL];
    gboolean stream_en[MAX_MODE];
    gint gop[MAX_MODE][MAX_CHANNEL];
    gint log_level;
    gint dbg_level;
    gboolean dbg_ts;        // enable timestamp debug (rtsp + capture)
    gboolean dbg_cap_ts;    // enable capture timestamp debug
    gboolean dbg_rtsp_ts;   // enable rtsp timestamp/latency debug
    const gchar *dotDir;
    const gchar *mntDir;
    const gchar *ohtName;
    const gchar *json_file;
    guint8 ch_enable;
    guint16 ch_rotate;
    ResMode resMode;
    gint main_fps[MAX_VIDEO_SRC];
    gint play_delay;
    gint config_delay;
    gboolean fault;
    gint duration;
    gboolean tcp_en;
    gint tcp_port;
    gboolean audio_en;
    const gchar *appname;
    gint split_diff_msec;
    gint split_max_msec;
    gint split_audio_min_msec;
    gint split_sec;
    gboolean input_en;
    const gchar *rtsp_port;
    const gchar *rtsp_id;
    const gchar *rtsp_passwd;
    gboolean overlay_en;
    gint width;
    gint height;
    gboolean crop_en[2];
    gboolean ipc_en;
    gint ipc_mid;
    gchar *enc;
    const gchar *muxer;
    gboolean dual_enc;
    gint rtsp_factory_latency_ms;
    gint rtsp_appsink_max_buffers;
    gint rtsp_factory_queue_max_buffers;
    gint rtsp_bin_queue_max_time_ms;

    gint v4l_subdev_csi0;
    gint v4l_subdev_csi1;
    gint v4l_video_csi0;
    gint v4l_video_csi1;
    CaptureConfig cap;
    gint wdt_timeout_short;
    gint wdt_timeout_long;
    gboolean srt_en;
    gboolean videorate_en;

    // Queue tuning parameters
    gint queue_main_src_time_ms;
    gint queue_enc_src_time_ms;
    gint queue_rec_sink_time_ms;
    gint queue_cap_src_time_ms;
} CmdArg;

#pragma pack(pop)

//extern gchar *program_name;
extern GstElement *pipeline;
extern GstElement *pipeline2;
extern GMainLoop *loop;
extern volatile sig_atomic_t is_interrupted;
extern CmdArg cmdArg;
extern gboolean is_live;
extern int g_link_disconnect_mask;

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
int check_sd_mount_flag(void);

/* Set an encoder property that only exists on some SoC/plugin builds.
 * vpuenc_h264 installs profile on i.MX8MP only and qp-min/qp-max on
 * i.MX8MP/i.MX8MM, so a plain g_object_set() against another build would
 * emit a GLib warning and silently drop the value.
 * Logs when the property is missing so a no-op is visible instead of
 * looking applied. */
void enc_set_optional_int(GstElement *enc, const gchar *prop, gint value, guint8 ch);

// Safe alternatives to system() calls
int safe_write_file(const char *path, const char *content);
int safe_mkdir_p(const char *path, mode_t mode);
int safe_exec_i2c(const char *cmd, int bus, int addr, int reg, int value, char *output, size_t output_size);

#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

#endif
