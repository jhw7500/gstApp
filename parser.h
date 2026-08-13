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
#define JSON_CAM_OBJ_NAME   "VHL_CAM"
#define JSON_CAP_OBJ_NAME   "capture"

#define DEFAULT_RECORD_BITRATE  4096
#define DEFAULT_RTSP_BITRATE    1024
/* vpuenc_h264 documents bitrate 0 as "automatic" (its own default): the VPU
 * wrapper then derives a rate from resolution and framerate instead of holding
 * a fixed CBR target. Accepted as-is so the mode stays reachable from edgeconf.
 *
 * Only negatives are rejected — deliberately no floor and no ceiling. The
 * wrapper's AdjustBitrate() already replaces anything under
 * VPU_ENC_MIN_BITRATE (10000 bps = 10 kbps) with the same auto-computed rate
 * that 0 gets, and clamps anything over VPU_ENC_MAX_BITRATE (60000 kbps).
 * Both ends are handled downstream, so a range check here would only reject
 * values the encoder copes with. */
#define BITRATE_AUTO            0
#define DEFAULT_MAIN_FPS        15
#define DEFAULT_RECORD_FPS      15
#define DEFAULT_RTSP_FPS        15
#define DEFAULT_CAPTURE_FPS     15
/* vpuenc_h264 "gop-size" = keyframe interval in frames.
 * 0 is a sentinel meaning "one GOP per second": check_arg() resolves it against
 * the stream's own fps, so the interval follows fps instead of going stale when
 * fps changes. With the default 15 fps this reproduces the interval gstApp has
 * always run with.
 * 0 must never reach the encoder — the plugin's periodic I-frame test
 * (gop_size && gop_count % gop_size, gstvpuenc.c) goes dead at 0, and what the
 * VPU does with idr_interval=0 is undocumented (closed VC8000E library).
 * Upper bound matches ENC_MAX_GOP_SIZE in the VPU wrapper, which silently
 * clamps anything larger. */
#define GOP_SIZE_FOLLOW_FPS     0
#define DEFAULT_GOP_SIZE        GOP_SIZE_FOLLOW_FPS
#define MIN_GOP_SIZE            0
#define MAX_GOP_SIZE            300
/* Codec-specific profile ranges from the vpuenc_h264/vpuenc_hevc properties.
 * PROFILE_UNSET is resolved after enc has been parsed, so an omitted profile
 * selects the matching plugin default instead of assuming H.264. */
#define PROFILE_UNSET           (-1)
#define DEFAULT_H264_PROFILE    9       /* Baseline */
#define MIN_H264_PROFILE        9
#define MAX_H264_PROFILE        12      /* Baseline, Main, High, High10 */
#define DEFAULT_H265_PROFILE    0       /* Main */
#define MIN_H265_PROFILE        0
#define MAX_H265_PROFILE        2       /* Main, Main Still Picture, Main10 */
#define DEFAULT_QUANT           (-1)    /* auto */
#define MIN_QUANT               (-1)
#define MAX_QUANT               51
#define DEFAULT_QP_MIN          0       /* 0 = unset -> hardware default */
#define DEFAULT_QP_MAX          0       /* 0 = unset -> hardware default */
#define MIN_QP                  0
#define MAX_QP                  51
#define DEFAULT_DURATION        1
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

#define DEFAULT_RTSP_FACTORY_LATENCY_MS 200
#define DEFAULT_RTSP_APPSINK_MAX_BUFFERS 2
#define DEFAULT_RTSP_FACTORY_QUEUE_MAX_BUFFERS 2
#define DEFAULT_RTSP_BIN_QUEUE_MAX_TIME_MS 200
/* video appsrc 백로그 상한 — IDR(실측 최대 ~140KB) + 여유. 16K~2M로 클램프 */
#define DEFAULT_RTSP_APPSRC_MAX_BYTES (192 * 1024)
#define DEFAULT_RTSP_FRAME_ID_SEI FALSE
#define DEFAULT_V4L2_SYNC_TRACE_SEC 0
#define DEFAULT_CHANNEL_SYNC_TRACE_SEC 0
#define DEFAULT_RTSP_SYNC_TRACE_SEC 0
#define SYNC_TRACE_MAX_SEC 3600
#define DEFAULT_RTSP_TEST_STALL_CH (-1)
#define DEFAULT_RTSP_TEST_STALL_AFTER_SEC 0
#define DEFAULT_RTSP_TEST_STALL_DURATION_SEC 0
#define RTSP_TEST_STALL_MAX_SEC 3600

// Platform device mapping defaults
// Target mapping (confirmed via sysfs):
// - i2c-2 (max9296 2-0048) -> /dev/v4l-subdev2 -> channels 0/1 (csi0)
// - i2c-1 (max9296 1-0048) -> /dev/v4l-subdev3 -> channels 2/3 (csi1)
#define DEFAULT_V4L_SUBDEV_CSI0 2
#define DEFAULT_V4L_SUBDEV_CSI1 3
#define DEFAULT_V4L_VIDEO_CSI0 4
#define DEFAULT_V4L_VIDEO_CSI1 3

#define DEFAULT_TCP_PORT    8555
#define DEFAULT_IPC_MID     0x65

#define DEFAULT_PLAY_DELAY  0
#define DEFAULT_CONFIG_DELAY  5
#define DEFAULT_CAPTURE_MAX_CNT 1
#define DEFAULT_CAPTURE_TIMEOUT 200
#define DEFAULT_CAPTURE_DELAY 0
#define DEFAULT_CAPTURE_QUALITY 85
#define DEFAULT_CAPTURE_QUEUE_SIZE 30
#define DEFAULT_CAPTURE_INSTANT 0
// [instant-snapshot] cap.instant modes
#define CAP_INSTANT_OFF   0   // no probe; behavior identical to before the feature
#define CAP_INSTANT_REF   1   // ref-hold: ~0 idle CPU, pins one shared G2D pool buffer
#define CAP_INSTANT_COPY  2   // deep-copy: no pool pin, per-frame copy cost while enabled
#define DEFAULT_SPLIT_SEC           0
#define DEFAULT_SPLIT_DIFF_MSEC     200
#define DEFAULT_SPLIT_MAX_MSEC      5000
#define DEFAULT_SPLIT_AUDIO_MIN_MSEC  59000
#define DEFAULT_WDT_TIMEOUT_LONG      300000
#define DEFAULT_WDT_TIMEOUT_SHORT     300000

#define DEFAULT_QUEUE_MAIN_SRC_TIME_MS 100
#define DEFAULT_QUEUE_ENC_SRC_TIME_MS  100
#define DEFAULT_QUEUE_REC_SINK_TIME_MS 300
#define DEFAULT_QUEUE_CAP_SRC_TIME_MS  500
/* enc 큐 깊이를 프레임 수로 지정한다 (0 = 미사용, queue 기본 10MB 가 그대로 구속).
 * 실측 근거: 4채널 HD60 에서 큐가 실제로 필요로 한 최대 점유는 2 버퍼였고,
 * 기본 10MB 는 dual HD 에서 1.42 프레임 / dual FHD 에서 0.63 프레임밖에 안 되어
 * 두 번째 버퍼를 받지 못해 드롭이 발생했다. 3 = 실측 필요량 2 + 마진 1. */
#define DEFAULT_QUEUE_ENC_SRC_FRAMES   3
#define DEFAULT_QUEUE_ENC_BUDGET_MB    0
#define DEFAULT_QUEUE_ENC_STAT_SEC     0

#define DEFAULT_START_VIDEO_TIME_PATH   "/tmp/start_video_time"
#define DEFAULT_DOT_PATH    "/tmp"
#define DEFAULT_ENC         ENC_H264
#define DEFAULT_MUXER       "mp4"
#define JHW_TESTx
#ifdef JHW_TEST
#define DEFAULT_MOUNT_PATH   "/home/user/jhw"
#define DEFAULT_CAPTURE_PATH "/home/user/jhw"
#else
#define DEFAULT_MOUNT_PATH   "/mnt/sd_cam"
#define DEFAULT_CAP_DIR       "capture"
#define DEFAULT_CAPTURE_PATH   "/mnt/sd_cam/capture"
#endif

#define MAX_FPS_HD  240
#define MAX_FPS_FHD 180

#define CFI_SEND_DATA_LEN   50
#define CFI_RECV_DATA_LEN   18
#define CFI_VERSION         0x300
#define CFI_CAP_REQ_CMD_ID  0x300
#define CFI_CAP_RES_CMD_ID  0x301
#define CTS_CAP_START_REQ_CMD_ID 0x900
#define CTS_CAP_STOP_REQ_CMD_ID 0x901

#pragma pack(push, 1)
union TCfiSendData {
  guint8 byte[CFI_SEND_DATA_LEN];
  struct THeader {
    guint16 len;
	  guint16 ver;
    guint8 sid[6];
    guint16 cmd_id;
    guint16 tx_id;
    guint8 channel;
    guint8 reserved;
    guint16 cap_cnt;
    guint8 prefix[32];
  } data;
};

union TCfiRecvData {
  guint8 byte[CFI_RECV_DATA_LEN];
  struct THeader {
    guint16 len;
	  guint16 ver;
    guint8 sid[6];
    guint16 cmd_id;
    guint16 tx_id;
    guint8 channel;
    guint8 reserved;
    guint16 cap_cnt;
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
    static gint json_sub_object_get_value(const gchar *file, const gchar *header, const gchar *sub_obj, const gchar *name, gpointer data);
    static json_object *json_find_obj (json_object * jobj, char *find_key);
    
private :
	
public :
	gboolean m_flagDestroy;
    CmdArg arg;
    GThread *captureThread[MAX_CHANNEL];
	
private :

};

#endif
