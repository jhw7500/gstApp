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
#include <unistd.h>
#include "captureBin.h"
#include "encoderBin.h"
#include "ipc.h"
#include "muxSinkBin.h"
#include "recordBin.h"
#include "rtspServerBin.h"
#include "videoBin.h"
#include "cfgjson.h"

#define LOG_KEY "CFG"

static void json_object_get_int_optional(json_object *obj, const gchar *name,
                                         gint *out) {
  if (!obj || !name || !out)
    return;

  json_object *vobj = json_object_object_get(obj, name);
  if (!vobj)
    return;

  enum json_type type = json_object_get_type(vobj);
  if (type != json_type_int)
    return;

  *out = json_object_get_int(vobj);
  __LOG(LOG_INFO, "[CFG][%s:%d] %s : %d", _FILE_, __LINE__, name, *out);
}

/*
 * Strict typed integer accessor.
 * - Missing key: keep *out unchanged (caller's pre-set default), no log.
 * - Wrong type:  keep *out unchanged, log LOG_ERR (config bug visible).
 * - Valid int:   assign and log LOG_INFO.
 */
static void json_get_int(json_object *obj, const gchar *name, gint *out) {
  if (!obj || !name || !out)
    return;

  json_object *vobj = json_object_object_get(obj, name);
  if (!vobj)
    return;

  enum json_type type = json_object_get_type(vobj);
  if (type != json_type_int) {
    __LOG(LOG_ERR,
          "[CFG][%s:%d] %s: expected int but got json_type[%d], keep default %d",
          _FILE_, __LINE__, name, type, *out);
    return;
  }

  *out = json_object_get_int(vobj);
  __LOG(LOG_INFO, "[CFG][%s:%d] %s : %d", _FILE_, __LINE__, name, *out);
}

static void json_get_uint(json_object *obj, const gchar *name, guint *out) {
  if (!obj || !name || !out)
    return;

  json_object *vobj = json_object_object_get(obj, name);
  if (!vobj)
    return;

  enum json_type type = json_object_get_type(vobj);
  if (type != json_type_int) {
    __LOG(LOG_ERR,
          "[CFG][%s:%d] %s: expected int but got json_type[%d], keep default %u",
          _FILE_, __LINE__, name, type, *out);
    return;
  }

  gint v = json_object_get_int(vobj);
  if (v < 0) {
    __LOG(LOG_ERR,
          "[CFG][%s:%d] %s: negative int %d for unsigned field, keep default %u",
          _FILE_, __LINE__, name, v, *out);
    return;
  }

  *out = (guint)v;
  __LOG(LOG_INFO, "[CFG][%s:%d] %s : %u", _FILE_, __LINE__, name, *out);
}

static void json_get_uint32(json_object *obj, const gchar *name, guint32 *out) {
  guint tmp = (guint)*out;
  json_get_uint(obj, name, &tmp);
  *out = (guint32)tmp;
}

/*
 * Strict typed integer-array accessor.
 * - Missing key:           keep *out unchanged.
 * - Wrong top-level type:  keep *out unchanged + LOG_ERR.
 * - Length mismatch:       keep *out unchanged + LOG_ERR.
 * - Element type mismatch: keep entire *out unchanged + LOG_ERR (no partial fill).
 */
static int g_cfg_errors = 0;

/* Wrapper over cfg_get_int_array (cfgjson.cpp): preserves the original logging
 * and counts config errors so the parser can emit a loud post-parse summary.
 * Silent keep-defaults hides real edgeconf mistakes — e.g. a bps array length
 * mismatch left the record bitrate stuck at the 4096 default. */
static void json_get_int_array(json_object *obj, const gchar *name, gint *out,
                               gsize n) {
  CfgArrStatus st = cfg_get_int_array(obj, name, out, n);
  switch (st) {
  case CFG_ARR_OK:
    for (gsize i = 0; i < n; i++)
      __LOG(LOG_INFO, "[CFG][%s:%d] %s[%zu] : %d", _FILE_, __LINE__, name, i,
            out[i]);
    break;
  case CFG_ARR_MISSING:
    break; /* optional key absent — keep defaults silently */
  case CFG_ARR_NOT_ARRAY:
    __LOG(LOG_ERR, "[CFG][%s:%d] %s: not a JSON array, keep defaults", _FILE_,
          __LINE__, name);
    g_cfg_errors++;
    break;
  case CFG_ARR_BAD_LEN:
    __LOG(LOG_ERR,
          "[CFG][%s:%d] %s: array length mismatch (expected %zu), keep defaults",
          _FILE_, __LINE__, name, n);
    g_cfg_errors++;
    break;
  case CFG_ARR_BAD_ELEM:
    __LOG(LOG_ERR, "[CFG][%s:%d] %s: non-int element, keep defaults", _FILE_,
          __LINE__, name);
    g_cfg_errors++;
    break;
  }
}

/* Range sanity for one per-stream vpuenc_h264 knob. Same policy as bps: never
 * abort startup on edgeconf noise — log loudly and fall back to the compiled-in
 * default, which equals the plugin's own default. */
static void enc_knob_sanity(gint *slot, gint lo, gint hi, gint def,
                            const gchar *name, const gchar *stream, gint ch) {
  if (*slot < lo || *slot > hi) {
    __LOG(LOG_ERR,
          "[%s][%s:%d] ch%d invalid %s %s %d, fallback to %d (range %d..%d)",
          LOG_KEY, _FILE_, __LINE__, ch, stream, name, *slot, def, lo, hi);
    *slot = def;
  }
}

static void enc_profile_sanity(gint *slot, gint lo, gint hi, gint def,
                               const gchar *stream, gint ch) {
  if (*slot == PROFILE_UNSET) {
    *slot = def;
    return;
  }

  enc_knob_sanity(slot, lo, hi, def, "profile", stream, ch);
}

/* gop 0 means "one GOP per second": resolve it against the stream's own fps so
 * the keyframe interval follows fps. 0 must not reach the encoder — see the
 * DEFAULT_GOP_SIZE comment in parser.h. */
static void enc_gop_resolve(gint *slot, gint fps, const gchar *stream, gint ch) {
  if (*slot != GOP_SIZE_FOLLOW_FPS)
    return;

  gint resolved = (fps > 0) ? fps : DEFAULT_RECORD_FPS;
  if (resolved > MAX_GOP_SIZE)
    resolved = MAX_GOP_SIZE;
  __LOG(LOG_INFO, "[%s][%s:%d] ch%d %s gop 0 -> %d (= fps, 1s keyframe interval)",
        LOG_KEY, _FILE_, __LINE__, ch, stream, resolved);
  *slot = resolved;
}

/* Single-encoder mode: one encoder feeds both record and rtsp, so the rtsp
 * slot is never consumed. Align it with rec so downstream reads stay honest. */
static void enc_knob_mirror(gint *slot, gint rec, const gchar *name, gint ch) {
  if (*slot != rec) {
    __LOG(LOG_INFO,
          "[%s][%s:%d] ch%d single-enc: align rtsp %s %d -> %d (= rec)",
          LOG_KEY, _FILE_, __LINE__, ch, name, *slot, rec);
    *slot = rec;
  }
}

void ParserClass::init_arg(gchar *argv) {
  // g_print("%s\n", __FUNCTION__);
  arg.appname = CHARNEXT(argv, '/');
  arg.ch_enable = DEFAULT_CH_ENABLE;
  arg.ch_rotate = DEFAULT_CH_ROTATE;
  arg.log_level = DEFAULT_LOG_LEVEL;
  arg.dbg_level = DEFAULT_DBG_LEVEL;
  arg.dbg_ts = FALSE;
  arg.dbg_cap_ts = FALSE;
  arg.dbg_rtsp_ts = FALSE;
  arg.mntDir = DEFAULT_MOUNT_PATH;
  arg.duration = DEFAULT_DURATION;
  arg.rtsp_port = DEFAULT_RTSP_PORT;
  arg.rtsp_id = DEFAULT_RTSP_ID;

  arg.main_fps[CSI_1] = DEFAULT_MAIN_FPS;
  arg.main_fps[CSI_2] = DEFAULT_MAIN_FPS;

  arg.ohtName = arg.appname;
  arg.width = DEFAULT_WIDTH;
  arg.height = DEFAULT_HEIGHT;

  arg.ioMode = IO_AUTO;
  arg.levelMode = MODE_NORMAL;
  arg.dotDir = DEFAULT_DOT_PATH;
  arg.cap.path = DEFAULT_CAP_DIR;
  arg.play_delay = DEFAULT_PLAY_DELAY;
  arg.config_delay = DEFAULT_CONFIG_DELAY;
  arg.fault = FALSE;
  arg.audio_en = FALSE;
  arg.cap.maxCnt = DEFAULT_CAPTURE_MAX_CNT;
  arg.input_en = FALSE;
  arg.overlay_en = FALSE;
  arg.split_diff_msec = DEFAULT_SPLIT_DIFF_MSEC;
  arg.split_max_msec = DEFAULT_SPLIT_MAX_MSEC;
  arg.split_audio_min_msec = DEFAULT_SPLIT_AUDIO_MIN_MSEC;
  g_free(arg.enc);
  arg.enc = g_strdup(DEFAULT_ENC);
  arg.muxer = DEFAULT_MUXER;
  arg.split_sec = DEFAULT_SPLIT_SEC;
  arg.stream_en[STREAM_REC] = TRUE;
  arg.stream_en[STREAM_RTSP] = TRUE;
  arg.stream_en[STREAM_CAP] = FALSE;
  arg.dual_enc = FALSE;
  arg.wdt_timeout_long = DEFAULT_WDT_TIMEOUT_LONG;
  arg.wdt_timeout_short = DEFAULT_WDT_TIMEOUT_SHORT;
  arg.videorate_en = TRUE;

  arg.rtsp_factory_latency_ms = DEFAULT_RTSP_FACTORY_LATENCY_MS;
  arg.rtsp_appsink_max_buffers = DEFAULT_RTSP_APPSINK_MAX_BUFFERS;
  arg.rtsp_factory_queue_max_buffers = DEFAULT_RTSP_FACTORY_QUEUE_MAX_BUFFERS;
  arg.rtsp_bin_queue_max_time_ms = DEFAULT_RTSP_BIN_QUEUE_MAX_TIME_MS;
  arg.rtsp_appsrc_max_bytes = DEFAULT_RTSP_APPSRC_MAX_BYTES;

  arg.queue_main_src_time_ms = DEFAULT_QUEUE_MAIN_SRC_TIME_MS;
  arg.queue_enc_src_time_ms = DEFAULT_QUEUE_ENC_SRC_TIME_MS;
  arg.queue_rec_sink_time_ms = DEFAULT_QUEUE_REC_SINK_TIME_MS;
  arg.queue_cap_src_time_ms = DEFAULT_QUEUE_CAP_SRC_TIME_MS;

  arg.v4l_subdev_csi0 = DEFAULT_V4L_SUBDEV_CSI0;
  arg.v4l_subdev_csi1 = DEFAULT_V4L_SUBDEV_CSI1;
  arg.v4l_video_csi0 = DEFAULT_V4L_VIDEO_CSI0;
  arg.v4l_video_csi1 = DEFAULT_V4L_VIDEO_CSI1;

  arg.cap.path = DEFAULT_CAP_DIR;
  arg.cap.maxCnt = DEFAULT_CAPTURE_MAX_CNT;
  arg.cap.dir = NULL;
  arg.cap.encoder = NULL;
  arg.cap.res_en = TRUE;
  arg.cap.delay = DEFAULT_CAPTURE_DELAY;
  arg.cap.timeout = DEFAULT_CAPTURE_TIMEOUT;
  arg.cap.padding = TRUE;
  arg.cap.quality = DEFAULT_CAPTURE_QUALITY;
  arg.cap.queue_size = DEFAULT_CAPTURE_QUEUE_SIZE;
  arg.cap.instant = DEFAULT_CAPTURE_INSTANT;
  arg.tcp_en = FALSE;
  arg.tcp_port = DEFAULT_TCP_PORT;

  arg.ipc_en = FALSE;
  arg.ipc_mid = DEFAULT_IPC_MID;

  for (guint8 i = 0; i < MAX_CHANNEL; i++) {
    arg.cam[i].enable = FALSE;
    arg.cam[i].hflip = FALSE;
    arg.cam[i].vflip = FALSE;
    arg.cam[i].bps[STREAM_REC] = DEFAULT_RECORD_BITRATE;
    arg.cam[i].bps[STREAM_RTSP] = DEFAULT_RTSP_BITRATE;
    arg.cam[i].gop[0] = DEFAULT_GOP_SIZE;
    arg.cam[i].gop[1] = DEFAULT_GOP_SIZE;
    arg.cam[i].profile[STREAM_REC] = PROFILE_UNSET;
    arg.cam[i].profile[STREAM_RTSP] = PROFILE_UNSET;
    arg.cam[i].quant[STREAM_REC] = DEFAULT_QUANT;
    arg.cam[i].quant[STREAM_RTSP] = DEFAULT_QUANT;
    arg.cam[i].qp_min[STREAM_REC] = DEFAULT_QP_MIN;
    arg.cam[i].qp_min[STREAM_RTSP] = DEFAULT_QP_MIN;
    arg.cam[i].qp_max[STREAM_REC] = DEFAULT_QP_MAX;
    arg.cam[i].qp_max[STREAM_RTSP] = DEFAULT_QP_MAX;
    arg.cam[i].ae_on = TRUE;
    arg.cam[i].ae_gain = DEFAULT_AE_GAIN;
    arg.cam[i].awb = DEFAULT_AWB;
    arg.cam[i].iso = DEFAULT_ISO;
    arg.cam[i].lsc = DEFAULT_LSC;
    arg.cam[i].exp_time = DEFAULT_EXP_TIME;
    arg.cam[i].led_flash_enable = FALSE;
    arg.cam[i].led_flash_wiper = 63;   /* MCP4018 mid-scale (matches driver default) */
    arg.cam[i].led_flash_delay = 0;    /* AR0234 0x3270 bit7:0 */
#if 0
        arg.cam[i].enable = FALSE;
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

json_object *ParserClass::json_find_obj(json_object *jobj, char *find_key) {
  size_t key_len = strlen(find_key);
  json_object_object_foreach(jobj, key, val) {
    if (strlen(key) == key_len && !memcmp(key, find_key, key_len))
      return val;
  }
  return NULL; // not found.
}

gint ParserClass::json_object_get_value(json_object *hobj, const gchar *name,
                                        gpointer data) {
  gint ret = 0;
  // json_object *vobj = json_find_obj(obj, (char *)name);
  json_object *vobj;

  vobj = json_object_object_get(hobj, name);
  if (vobj == NULL) {
    __LOG(LOG_CRIT, "[CFG][%s:%d] not exist : %s", _FILE_, __LINE__, name);
    return -1;
  }

  enum json_type type = json_object_get_type(vobj);

  if (type == json_type_object) {
    gchar **val = (gchar **)data;
    *val = (gchar *)json_object_get_string(vobj);
    __LOG(LOG_ERR, "[CFG][%s:%d] Type: Json object, name: %s, val: %s", _FILE_,
          __LINE__, name, *val);
    // return json_object_get_value(vobj, name, data);
  } else if (type == json_type_string) {
    gchar **val = (gchar **)data;
    *val = (gchar *)json_object_get_string(vobj);
    __LOG(LOG_INFO, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name, *val);
  } else if (type == json_type_int) {
    gint *val = (gint *)data;
    *val = json_object_get_int(vobj);
    __LOG(LOG_INFO, "[CFG][%s:%d] %s : %d", _FILE_, __LINE__, name, *val);
  } else if (type == json_type_boolean) {
    gboolean *val = (gboolean *)data;
    *val = json_object_get_boolean(vobj);
    __LOG(LOG_INFO, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name,
          *val ? "TRUE" : "FALSE");
  } else if (type == json_type_double) {
    gdouble *val = (gdouble *)data;
    *val = json_object_get_double(vobj);
    __LOG(LOG_INFO, "[CFG][%s:%d] %s : %f", _FILE_, __LINE__, name, *val);
  } else if (type == json_type_array) {
    array_list *arr = json_object_get_array(vobj);
    // g_print("len:%ld arr->size:%ld arr->len:%ld\n",
    // json_object_array_length(vobj), arr->size, arr->length);
    for (size_t i = 0; i < arr->length; i++) {
      json_object *retrieved_obj = (json_object *)array_list_get_idx(arr, i);
      type = json_object_get_type(retrieved_obj);
      if (type == json_type_string) {
        gchar **arr = (gchar **)data;
        arr[i] = (gchar *)json_object_get_string(retrieved_obj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %s", _FILE_, __LINE__, name, i,
              arr[i]);
      } else if (type == json_type_int) {
        gint *arr = (gint *)data;
        arr[i] = json_object_get_int(retrieved_obj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %d", _FILE_, __LINE__, name, i,
              arr[i]);
      } else if (type == json_type_boolean) {
        gboolean *arr = (gboolean *)data;
        arr[i] = json_object_get_boolean(retrieved_obj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %s", _FILE_, __LINE__, name, i,
              arr[i] ? "TRUE" : "FALSE");
      } else if (type == json_type_double) {
        gdouble *arr = (gdouble *)data;
        arr[i] = json_object_get_double(retrieved_obj);
        __LOG(LOG_INFO, "[CFG][%s:%d] %s[%d] : %f", _FILE_, __LINE__, name, i,
              arr[i]);
      } else if (type == json_type_null) {
        __LOG(LOG_ERR, "[CFG][%s:%d] not exist : %s", _FILE_, __LINE__, name);
      } else {
        __LOG(LOG_ERR, "[CFG][%s:%d] unsupport type : %d", _FILE_, __LINE__,
              type);
      }
    }
  } else if (type == json_type_null) {
    __LOG(LOG_ERR, "[CFG][%s:%d] not exist : %s", _FILE_, __LINE__, name);
  } else {
    __LOG(LOG_ERR, "[CFG][%s:%d] unsupport type : %d", _FILE_, __LINE__, type);
  }

  // ret = json_object_put(vobj);

  return ret;
}

ParserClass::ParserClass() {
  // 생성자 코드 추가
  arg.enc = NULL;
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

ParserClass::~ParserClass() {
  // 소멸자 코드 추가
  g_free(arg.enc);
  arg.enc = NULL;
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

ParserClass *ParserClass::getInstance() {
  static ParserClass instance;
  return &instance;
}

gint ParserClass::json_sub_object_get_value(const gchar *file,
                                            const gchar *header,
                                            const gchar *sub_obj,
                                            const gchar *name, gpointer data) {
  json_object *jobj = NULL;
  json_object *hobj = NULL;
  json_object *sobj = NULL;

  jobj = json_object_from_file(file);
  if (jobj == NULL) {
    __LOG(LOG_CRIT, "[%s][%s:%d] json file open fail : %s", LOG_KEY, _FILE_,
          __LINE__, file);
    return -1;
  }

  enum json_type type = json_object_get_type(jobj);

  do {
    if (type != json_type_object) {
      __LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_,
            __LINE__, type);
      break;
    }

    // hobj = json_find_obj(jobj, "VHL_CAM");
    hobj = json_object_object_get(jobj, header);
    if (hobj == NULL) {
      __LOG(LOG_CRIT, "[%s][%s:%d] not exist header : %s", LOG_KEY, _FILE_,
            __LINE__, header);
      return -1;
    }
    type = json_object_get_type(hobj);
    if (type != json_type_object) {
      __LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_,
            __LINE__, type);
      break;
    }

    sobj = json_object_object_get(hobj, sub_obj);
    if (sobj == NULL) {
      __LOG(LOG_CRIT, "[%s][%s:%d] not exist sub_obj : %s", LOG_KEY, _FILE_,
            __LINE__, sub_obj);
      return -1;
    }
    type = json_object_get_type(hobj);
    if (type != json_type_object) {
      __LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_,
            __LINE__, type);
      break;
    }
    json_object_get_value(sobj, name, data);
  } while (0);

  return 0;
}

gint ParserClass::json_parser(const gchar *path, const gchar *header) {
  gint ret = -1;

  json_object *jobj = NULL;
  json_object *hobj = NULL;
  json_object *sobj = NULL;
  json_object *vobj = NULL;
  // const gchar* ptr;

  arg.json_file = search_file(path, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
  __LOG(LOG_INFO, "[%s][%s:%d] json file name : %s", LOG_KEY, _FILE_, __LINE__,
        arg.json_file);

  if (strstr(arg.json_file, JSON_NAME_PREFIX) == NULL ||
      strstr(arg.json_file, JSON_NAME_SUFFIX) == NULL) {
    __LOG(LOG_CRIT, "[%s][%s:%d] json file name not match %s %s", LOG_KEY,
          _FILE_, __LINE__, JSON_NAME_PREFIX, JSON_NAME_SUFFIX);
    return ret;
  }

  jobj = json_object_from_file(arg.json_file);
  enum json_type type = json_object_get_type(jobj);

  do {
    if (type != json_type_object) {
      __LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_,
            __LINE__, type);
      break;
    }

    // hobj = json_find_obj(jobj, "VHL_CAM");
    hobj = json_object_object_get(jobj, header);
    type = json_object_get_type(hobj);

    if (type != json_type_object) {
      __LOG(LOG_ERR, "[%s][%s:%d] data not json type[%d]", LOG_KEY, _FILE_,
            __LINE__, type);
      // break;
    }

    json_object_get_value(hobj, "vhl_name", &arg.ohtName);
    json_object_get_value(hobj, "id", &arg.rtsp_id);
    json_object_get_value(hobj, "tmp_path", &arg.mntDir);
    json_get_int(hobj, "cam_width", &arg.width);
    json_get_int(hobj, "cam_height", &arg.height);
    json_get_int(hobj, "recording_time", &arg.duration);
    json_get_int(hobj, "log_level", &arg.log_level);
    json_get_int(hobj, "debug_level", &arg.dbg_level);
    json_get_int(hobj, "fps", &arg.main_fps[CSI_1]);
    json_get_int(hobj, "fps", &arg.main_fps[CSI_2]);
    json_object *enc_obj = json_object_object_get(hobj, "enc");
    if (enc_obj) {
      if (json_object_get_type(enc_obj) == json_type_string) {
        gchar *enc = g_strdup(json_object_get_string(enc_obj));
        g_free(arg.enc);
        arg.enc = enc;
        __LOG(LOG_INFO, "[CFG][%s:%d] enc : %s", _FILE_, __LINE__, arg.enc);
      } else {
        __LOG(LOG_WARNING,
              "[CFG][%s:%d] enc must be a string, using default : %s",
              _FILE_, __LINE__, DEFAULT_ENC);
      }
    }
    json_object_get_value(hobj, "muxer", &arg.muxer);
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
    if (arg.stream_en[STREAM_CAP]) {
      json_get_int(sobj, "delay", &arg.cap.delay);
      json_get_int(sobj, "timeout", &arg.cap.timeout);
      json_object_get_value(sobj, "encoder", &arg.cap.encoder);
      // Optional: absolute capture output directory.
      // Do not use json_object_get_value() because missing keys log CRIT.
      {
        json_object *dir_obj = json_object_object_get(sobj, "path");
        if (dir_obj && json_object_get_type(dir_obj) == json_type_string) {
          const char *dir_str = json_object_get_string(dir_obj);
          if (dir_str && dir_str[0] == '/') {
            arg.cap.dir = dir_str;
            __LOG(LOG_INFO, "[%s][%s:%d] capture dir: %s", LOG_KEY, _FILE_,
                  __LINE__, arg.cap.dir);
          } else if (dir_str && dir_str[0] != '\0') {
            __LOG(LOG_WARNING,
                  "[%s][%s:%d] capture dir must be absolute (ignored): %s",
                  LOG_KEY, _FILE_, __LINE__, dir_str);
          }
        }
      }
      // json_object_get_value(sobj, "padding", &arg.cap.padding);
      json_object_get_value(sobj, "record", &arg.cap.record_en);
      json_object_get_value(sobj, "rtsp", &arg.cap.rtsp_en);
      json_get_int(sobj, "quality", &arg.cap.quality);
      json_get_int(sobj, "queue_size", &arg.cap.queue_size);
      json_object_get_value(sobj, "response", &arg.cap.res_en);
      json_get_int(sobj, "instant", &arg.cap.instant);

      arg.stream_en[STREAM_REC] = arg.cap.record_en;
      arg.stream_en[STREAM_RTSP] = arg.cap.rtsp_en;
      arg.ipc_en = TRUE;
    }

    // Optional RTSP tuning knobs. Keep optional to avoid breaking older
    // edgeconf.
    {
      json_object *rtsp_obj = json_object_object_get(hobj, "rtsp_tune");
      json_object *tune_obj = rtsp_obj ? rtsp_obj : sobj;
      json_object_get_int_optional(tune_obj, "rtsp_factory_latency_ms",
                                   &arg.rtsp_factory_latency_ms);
      json_object_get_int_optional(tune_obj, "rtsp_appsink_max_buffers",
                                   &arg.rtsp_appsink_max_buffers);
      json_object_get_int_optional(tune_obj, "rtsp_factory_queue_max_buffers",
                                   &arg.rtsp_factory_queue_max_buffers);
      json_object_get_int_optional(tune_obj, "rtsp_bin_queue_max_time_ms",
                                   &arg.rtsp_bin_queue_max_time_ms);
      json_object_get_int_optional(tune_obj, "rtsp_appsrc_max_bytes",
                                   &arg.rtsp_appsrc_max_bytes);
    }

    // [Queue Tuning] Common/Record/Capture pipeline settings
    {
      json_object *queue_obj = json_object_object_get(hobj, "queue_tune");
      // Fallback to main object if queue_tune block is missing (optional)
      json_object *target_obj = queue_obj ? queue_obj : hobj;
      
      json_object_get_int_optional(target_obj, "main_src_time_ms", &arg.queue_main_src_time_ms);
      json_object_get_int_optional(target_obj, "enc_src_time_ms", &arg.queue_enc_src_time_ms);
      json_object_get_int_optional(target_obj, "rec_sink_time_ms", &arg.queue_rec_sink_time_ms);
      json_object_get_int_optional(target_obj, "cap_src_time_ms", &arg.queue_cap_src_time_ms);
    }

    // Optional platform device mapping overrides.
    {
      json_object *map_obj = json_object_object_get(hobj, "v4l_map");
      if (!map_obj)
        map_obj = json_object_object_get(hobj, "device_map");

      json_object_get_int_optional(map_obj, "csi0_subdev",
                                   &arg.v4l_subdev_csi0);
      json_object_get_int_optional(map_obj, "csi1_subdev",
                                   &arg.v4l_subdev_csi1);
      json_object_get_int_optional(map_obj, "csi0_video", &arg.v4l_video_csi0);
      json_object_get_int_optional(map_obj, "csi1_video", &arg.v4l_video_csi1);
    }

    /*
     * Derive enable/rotation bitmasks from per-channel JSON.
     * init_arg() seeds defaults (e.g., DEFAULT_CH_ROTATE). If we don't reset
     * here, stale default bits can leak into runtime settings.
     */
    arg.ch_enable = 0;
    arg.ch_rotate = 0;
    g_cfg_errors = 0;

    for (guint8 i = 0; i < MAX_CHANNEL; i++) {
      gchar *i2c_key = g_strdup_printf("i2c%d", i / 2 ? 1 : 2);
      sobj = json_object_object_get(hobj, i2c_key);
      g_free(i2c_key);
      /* Accept both ext_time and exp_time for backward compatibility. */
      {
        json_object *ext_obj = json_object_object_get(sobj, "ext_time");
        if (ext_obj && json_object_get_type(ext_obj) == json_type_int) {
          arg.cam[i].exp_time = (guint32)json_object_get_int(ext_obj);
          __LOG(LOG_INFO, "[CFG][%s:%d] ext_time : %u", _FILE_, __LINE__,
                arg.cam[i].exp_time);
        } else {
          json_get_uint32(sobj, "exp_time", &arg.cam[i].exp_time);
        }
      }
      gchar *ch_key = g_strdup_printf("ch%d", i);
      vobj = json_object_object_get(sobj, ch_key);
      g_free(ch_key);
      json_object_get_value(vobj, "enable", &arg.cam[i].enable);
      json_object_get_value(vobj, "hflip", &arg.cam[i].hflip);
      json_object_get_value(vobj, "vflip", &arg.cam[i].vflip);
      json_get_int_array(vobj, "bps", arg.cam[i].bps,
                         sizeof(arg.cam[i].bps) / sizeof(arg.cam[i].bps[0]));
      /* vpuenc_h264 tuning, all [rec, rtsp] pairs like bps. Absent keys keep
       * the init_arg defaults, which mirror the plugin's own defaults. */
      json_get_int_array(vobj, "gop", arg.cam[i].gop,
                         sizeof(arg.cam[i].gop) / sizeof(arg.cam[i].gop[0]));
      json_get_int_array(vobj, "profile", arg.cam[i].profile,
                         sizeof(arg.cam[i].profile) / sizeof(arg.cam[i].profile[0]));
      json_get_int_array(vobj, "quant", arg.cam[i].quant,
                         sizeof(arg.cam[i].quant) / sizeof(arg.cam[i].quant[0]));
      json_get_int_array(vobj, "qp_min", arg.cam[i].qp_min,
                         sizeof(arg.cam[i].qp_min) / sizeof(arg.cam[i].qp_min[0]));
      json_get_int_array(vobj, "qp_max", arg.cam[i].qp_max,
                         sizeof(arg.cam[i].qp_max) / sizeof(arg.cam[i].qp_max[0]));
      json_object_get_value(vobj, "ae_on", &arg.cam[i].ae_on);
      json_get_uint(vobj, "ae_gain", &arg.cam[i].ae_gain);
      /* Optional: AWB preset name. Falls back to DEFAULT_AWB when absent. */
      {
        json_object *awb_obj = json_object_object_get(vobj, "awb");
        if (awb_obj && json_object_get_type(awb_obj) == json_type_string)
          arg.cam[i].awb = json_object_get_string(awb_obj);
      }

      /* Optional: LED flash sub-object. Missing keys keep init_arg defaults. */
      {
        json_object *lf_obj = json_object_object_get(vobj, "led_flash");
        if (lf_obj && json_object_get_type(lf_obj) == json_type_object) {
          json_object_get_value(lf_obj, "enable",      &arg.cam[i].led_flash_enable);
          json_get_uint(lf_obj, "wiper",       &arg.cam[i].led_flash_wiper);
          json_get_uint(lf_obj, "flash_delay", &arg.cam[i].led_flash_delay);
        }
      }

      arg.ch_enable |= (arg.cam[i].enable << i);
      arg.ch_rotate |= (arg.cam[i].hflip << (i * 2));
      arg.ch_rotate |= (arg.cam[i].vflip << (i * 2 + 1));

      for (guint8 k = 0; k < MAX_MODE; k++)
        arg.fps[k][i] = arg.main_fps[CSI_1];
    }

    // Read SRT enabled status from ord_vcm_conf.json
    {
      const gchar *ord_json = "/root/shared_v/ord_vcm_conf.json";
      if (access(ord_json, R_OK) != 0) {
        ord_json = "/home/root/ord_vcm_conf.json";
      }

      json_object *ord_obj = json_object_from_file(ord_json);
      if (ord_obj) {
        json_object *vcm_obj = json_object_object_get(ord_obj, "VCM");
        if (vcm_obj) {
          json_object *srt_obj = json_object_object_get(vcm_obj, "srt_enable");
          if (srt_obj) {
            arg.srt_en = json_object_get_boolean(srt_obj);
            __LOG(LOG_INFO, "[CFG][%s:%d] srt_enable : %s", _FILE_, __LINE__,
                  arg.srt_en ? "TRUE" : "FALSE");
          }
        }
        json_object_put(ord_obj);
      } else {
        arg.srt_en = FALSE;
      }
    }

  } while (0);

  // ret = json_object_put(jobj);
  // ret = json_object_put(hobj);

  return 0;
}

gint ParserClass::arg_parser(int *argc, char **argv[]) {
  // arg* arg = (arg *)data;
  GOptionContext *ctx;
  GError *err = NULL;
  gchar str[128];
  gint ret = 0;
  guint8 i;
  gchar *enc_option = NULL;

  GOptionEntry entries[] = {
      {"mode", 'm', 0, G_OPTION_ARG_INT, &arg.ioMode,
       "io mode select 0(auto), 1(rw), 2(mmap), 3(userptr), 4(dmabuf), "
       "5(dmabuf-import), default(0)",
       "INT"},
      {"test", 'T', 0, G_OPTION_ARG_INT, &arg.levelMode,
       "test mode select 0(normal), 1(test), default(0)", "INT"},
      {"dbg", 'g', 0, G_OPTION_ARG_INT, &arg.dbg_level,
       "debug level, default(5)", "INT"},
      {"dbg-ts", 0, 0, G_OPTION_ARG_NONE, &arg.dbg_ts,
       "enable timestamp debug logs (capture + rtsp)", NULL},
      {"dbg-cap-ts", 0, 0, G_OPTION_ARG_NONE, &arg.dbg_cap_ts,
       "enable capture timestamp debug logs", NULL},
      {"dbg-rtsp-ts", 0, 0, G_OPTION_ARG_NONE, &arg.dbg_rtsp_ts,
       "enable rtsp timestamp/latency debug logs", NULL},
      {"log", 'l', 0, G_OPTION_ARG_INT, &arg.log_level, "log level, default(6)",
       "INT"},
      {"dot", 'o', 0, G_OPTION_ARG_STRING, &arg.dotDir,
       "save dot representation of pipeline to FILE and exit, default(/tmp)",
       "STRING"},
      {"channel", 'c', 0, G_OPTION_ARG_INT, &arg.ch_enable,
       "cam channel enable bit, default(0x00)", "HEX"},
      {"rotation", 'r', 0, G_OPTION_ARG_INT, &arg.ch_rotate,
       "cam channel rotation bit, default(0x00)", "HEX"},
      {"mnt", 'O', 0, G_OPTION_ARG_STRING, &arg.mntDir,
       "save video & audio file to directory, default(/mnt/sd_cam)", "STRING"},
      {"width", 'w', 0, G_OPTION_ARG_INT, &arg.width,
       "cam width HD(1280), FHD(1920), default(1920)", "INT"},
      {"height", 'h', 0, G_OPTION_ARG_INT, &arg.height,
       "cam height HD(720), FHD(1080), default(1080)", "INT"},
      {"oht", 'n', 0, G_OPTION_ARG_STRING, &arg.ohtName,
       "oht name, default(APPNAME)", "STRING"},
      {"delay", 'd', 0, G_OPTION_ARG_INT, &arg.play_delay,
       "from pause to play delay, default(0)", "SECOND"},
      {"cdelay", 'G', 0, G_OPTION_ARG_INT, &arg.config_delay,
       "camera config delay, default(5)", "SECOND"},
      {"duration", 't', 0, G_OPTION_ARG_INT, &arg.duration,
       "recoding file split duration, default(1)", "MINUTE"},
      {"rec", 'e', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_REC],
       "video recording enable, default(1)", "INT"},
      {"rtsp", 'E', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_RTSP],
       "rtsp streaming enable, default(1)", "INT"},
      {"cap", 'a', 0, G_OPTION_ARG_INT, &arg.stream_en[STREAM_CAP],
       "video capturing enable, default(0)", "INT"},
      {"audio", 's', 0, G_OPTION_ARG_INT, &arg.audio_en,
       "audio recording enable, default(FALSE)", "INT"},
      {"padding", 'J', 0, G_OPTION_ARG_INT, &arg.cap.padding,
       "padding enable, default(TRUE)", "INT"},
      {"capenc", 'N', 0, G_OPTION_ARG_STRING, &arg.cap.encoder,
       "video capture encoder(jpeg, turbo/turbojpeg, raw, png), default(jpeg)",
       "STRING"},
      {"capres", 'R', 0, G_OPTION_ARG_INT, &arg.cap.res_en,
       "video capture response enable, default(TRUE)", "INT"},
      {"capmax", 'x', 0, G_OPTION_ARG_INT, &arg.cap.maxCnt,
       "capture max count, default(3)", "INT"},
      {"capdelay", 'A', 0, G_OPTION_ARG_INT, &arg.cap.delay,
       "video capture delay(msec), default(0)", "INT"},
      {"capquality", 'q', 0, G_OPTION_ARG_INT, &arg.cap.quality,
       "video capture quality, default(85)", "INT"},
      {"capqueue", 0, 0, G_OPTION_ARG_INT, &arg.cap.queue_size,
       "capture async queue size, default(30)", "INT"},
      {"capinstant", 0, 0, G_OPTION_ARG_INT, &arg.cap.instant,
       "instant-snapshot single capture: 0=off, 1=ref-hold(light), 2=deep-copy(safe), default(0)", "INT"},
      {"capdir", 'I', 0, G_OPTION_ARG_STRING, &arg.cap.path,
       "save capture file to directory, default capture", "STRING"},
      {"etcp", 'C', 0, G_OPTION_ARG_INT, &arg.tcp_en,
       "tcp server enable, default(FALSE)", "INT"},
      {"ein", 'i', 0, G_OPTION_ARG_INT, &arg.input_en,
       "terminal input enable, default(FALSE)", "INT"},
      {"rport", 'P', 0, G_OPTION_ARG_STRING, &arg.rtsp_port,
       "rtsp port number, default(8554)", "STRING"},
      {"id", 'u', 0, G_OPTION_ARG_STRING, &arg.rtsp_id,
       "rtsp id, default(user)", "STRING"},
      {"passwd", 'p', 0, G_OPTION_ARG_STRING, &arg.rtsp_passwd,
       "rtsp passwd, default(user)", "STRING"},
      {"eover", 'v', 0, G_OPTION_ARG_INT, &arg.overlay_en,
       "overlay enable, default(FALSE)", "INT"},
      {"split_diff", 'D', 0, G_OPTION_ARG_INT, &arg.split_diff_msec,
       "split diff msec, default(100)", "INT"},
      {"split_max", 'X', 0, G_OPTION_ARG_INT, &arg.split_max_msec,
       "split max msec, default(2000)", "INT"},
      {"split_sec", 'S', 0, G_OPTION_ARG_INT, &arg.split_sec,
       "split sec, default(0)", "INT"},
      {"muxer", 'Q', 0, G_OPTION_ARG_STRING, &arg.muxer,
       "muxer(mp4, qt, ts), default(mp4)", "STRING"},
      {"enc", 0, 0, G_OPTION_ARG_STRING, &enc_option,
       "encoder(h264, h265), default(h264)", "STRING"},
      {"eipc", 'f', 0, G_OPTION_ARG_INT, &arg.ipc_en,
       "ipc enable, default(FALSE)", "INT"},
      {"ipc_mid", 'F', 0, G_OPTION_ARG_INT, &arg.ipc_mid,
       "ipc message id, default(0x65)", "INT"},
      {"dual_enc", 'U', 0, G_OPTION_ARG_INT, &arg.dual_enc,
       "dual encoder, default(FALSE)", "INT"},
      {"evrate", 'V', 0, G_OPTION_ARG_INT, &arg.videorate_en,
       "videorate enable, default(TRUE)", "INT"},
      {"wdt_l", 'B', 0, G_OPTION_ARG_INT, &arg.wdt_timeout_long,
       "watchdog timeout long, default(30000)", "INT"},
      {"wdt_s", 'b', 0, G_OPTION_ARG_INT, &arg.wdt_timeout_short,
       "watchdog timeout short, default(20000)", "INT"},
      {"fault", 0, 0, G_OPTION_ARG_INT, &arg.fault,
       "fault debug setup, default(FALSE)", "INT"},
      {"tport", 0, 0, G_OPTION_ARG_INT, &arg.tcp_port,
       "tcp port num, default(8555)", "INT"},
      {"fmain0", 0, 0, G_OPTION_ARG_INT, &arg.main_fps[CSI_1],
       "csi1 main frame per second, default(15)", "INT"},
      {"fmain1", 0, 0, G_OPTION_ARG_INT, &arg.main_fps[CSI_2],
       "csi2 main frame per second, default(15)", "INT"},
      {"frec0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][0],
       "ch0 record frame per second, default(15)", "INT"},
      {"frec1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][1],
       "ch1 record frame per second, default(15)", "INT"},
      {"frec2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][2],
       "ch2 record frame per second, default(15)", "INT"},
      {"frec3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_REC][3],
       "ch3 record frame per second, default(15)", "INT"},
      {"frtsp0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][0],
       "ch0 rtsp frame per second, default(15)", "INT"},
      {"frtsp1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][1],
       "ch1 rtsp frame per second, default(15)", "INT"},
      {"frtsp2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][2],
       "ch2 rtsp frame per second, default(15)", "INT"},
      {"frtsp3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_RTSP][3],
       "ch3 rtsp frame per second, default(15)", "INT"},
      {"fcap0", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][0],
       "ch0 capture frame per second, default(15)", "INT"},
      {"fcap1", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][1],
       "ch1 capture frame per second, default(15)", "INT"},
      {"fcap2", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][2],
       "ch2 capture frame per second, default(15)", "INT"},
      {"fcap3", 0, 0, G_OPTION_ARG_INT, &arg.fps[STREAM_CAP][3],
       "ch3 capture frame per second, default(15)", "INT"},
      {"brec0", 0, 0, G_OPTION_ARG_INT, &arg.cam[0].bps[STREAM_REC],
       "ch0 record Kbyte per second, default(4096)", "INT"},
      {"brec1", 0, 0, G_OPTION_ARG_INT, &arg.cam[1].bps[STREAM_REC],
       "ch1 record Kbyte per second, default(4096)", "INT"},
      {"brec2", 0, 0, G_OPTION_ARG_INT, &arg.cam[2].bps[STREAM_REC],
       "ch2 record Kbyte per second, default(4096)", "INT"},
      {"brec3", 0, 0, G_OPTION_ARG_INT, &arg.cam[3].bps[STREAM_REC],
       "ch3 record Kbyte per second, default(4096)", "INT"},
      {"brtsp0", 0, 0, G_OPTION_ARG_INT, &arg.cam[0].bps[STREAM_RTSP],
       "ch0 rtsp Kbyte per second, default(1024)", "INT"},
      {"brtsp1", 0, 0, G_OPTION_ARG_INT, &arg.cam[1].bps[STREAM_RTSP],
       "ch1 rtsp Kbyte per second, default(1024)", "INT"},
      {"brtsp2", 0, 0, G_OPTION_ARG_INT, &arg.cam[2].bps[STREAM_RTSP],
       "ch2 rtsp Kbyte per second, default(1024)", "INT"},
      {"brtsp3", 0, 0, G_OPTION_ARG_INT, &arg.cam[3].bps[STREAM_RTSP],
       "ch3 rtsp Kbyte per second, default(1024)", "INT"},
      {"grec0", 0, 0, G_OPTION_ARG_INT, &arg.cam[0].gop[STREAM_REC],
       "ch0 rec gop size, 0=fps(1s), default(0)", "INT"},
      {"grec1", 0, 0, G_OPTION_ARG_INT, &arg.cam[1].gop[STREAM_REC],
       "ch1 rec gop size, 0=fps(1s), default(0)", "INT"},
      {"grec2", 0, 0, G_OPTION_ARG_INT, &arg.cam[2].gop[STREAM_REC],
       "ch2 rec gop size, 0=fps(1s), default(0)", "INT"},
      {"grec3", 0, 0, G_OPTION_ARG_INT, &arg.cam[3].gop[STREAM_REC],
       "ch3 rec gop size, 0=fps(1s), default(0)", "INT"},
      {"grtsp0", 0, 0, G_OPTION_ARG_INT, &arg.cam[0].gop[STREAM_RTSP],
       "ch0 rtsp gop size, 0=fps(1s), default(0)", "INT"},
      {"grtsp1", 0, 0, G_OPTION_ARG_INT, &arg.cam[1].gop[STREAM_RTSP],
       "ch1 rtsp gop size, 0=fps(1s), default(0)", "INT"},
      {"grtsp2", 0, 0, G_OPTION_ARG_INT, &arg.cam[2].gop[STREAM_RTSP],
       "ch2 rtsp gop size, 0=fps(1s), default(0)", "INT"},
      {"grtsp3", 0, 0, G_OPTION_ARG_INT, &arg.cam[3].gop[STREAM_RTSP],
       "ch3 rtsp gop size, 0=fps(1s), default(0)", "INT"},
      {NULL}};

  ctx = g_option_context_new("- Your application");
  g_option_context_add_main_entries(ctx, entries, NULL);
  g_option_context_add_group(ctx, gst_init_get_option_group());

  ret = g_option_context_parse(ctx, argc, argv, &err);
  if (!ret) {
    __LOG(LOG_CRIT, "[%s][%s:%d] Failed to initialize : %s", LOG_KEY, _FILE_,
          __LINE__, err->message);
    g_error_free(err);
    g_free(enc_option);
    return ret;
  }

  if (enc_option) {
    g_free(arg.enc);
    arg.enc = enc_option;
  }

  if (arg.dbg_ts) {
    arg.dbg_cap_ts = TRUE;
    arg.dbg_rtsp_ts = TRUE;
  }

#if 1
  gint max[2] = {0, 0};

  for (i = 0; i < 3; i++) {
    for (guint8 k = 0; k < MAX_CHANNEL; k++) {
      if (arg.fps[i][k] > max[k / 2])
        max[k / 2] = arg.fps[i][k];
    }
  }

  if (max[0] > 0)
    arg.main_fps[CSI_2] = max[0];
  if (max[1] > 0)
    arg.main_fps[CSI_1] = max[1];

  __LOG(LOG_INFO, "[%s][%s:%d] arg.main_fps[CSI_1]:%d, arg.main_fps[CSI_2]:%d",
        LOG_KEY, _FILE_, __LINE__, arg.main_fps[CSI_1], arg.main_fps[CSI_2]);
#endif
  //__LOG(LOG_NOTICE, "[%s][%s:%d] arg.ch_enable : 0x%x", LOG_KEY, _FILE_,
  //__LINE__, arg.ch_enable);
  for (i = 0; i < MAX_CHANNEL; i++) {
    arg.cam[i].enable = (arg.ch_enable >> i) & 0x01;
    arg.cam[i].hflip = (arg.ch_rotate >> (i * 2)) & 0x01;
    arg.cam[i].vflip = (arg.ch_rotate >> (i * 2 + 1)) & 0x01;
  }

  // Use safe_write_file instead of system("echo ... > ...")
  sprintf(str, "%d", arg.ch_enable & 0x03);
  __LOG(LOG_INFO, "[%s][%s:%d] Writing %s to %s", LOG_KEY, _FILE_, __LINE__,
        str, DEFAULT_ENABLE_PATH_01);
  ret = safe_write_file(DEFAULT_ENABLE_PATH_01, str);
  if (ret < 0) {
    __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
    return ret;
  }

  sprintf(str, "%d", (arg.ch_enable >> 2) & 0x03);
  __LOG(LOG_INFO, "[%s][%s:%d] Writing %s to %s", LOG_KEY, _FILE_, __LINE__,
        str, DEFAULT_ENABLE_PATH_23);
  ret = safe_write_file(DEFAULT_ENABLE_PATH_23, str);
  if (ret < 0) {
    __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
    return ret;
  }

  sprintf(str, "%d", arg.ch_rotate & 0x0f);
  __LOG(LOG_INFO, "[%s][%s:%d] Writing %s to %s", LOG_KEY, _FILE_, __LINE__,
        str, DEFAULT_ROTATE_PATH_01);
  ret = safe_write_file(DEFAULT_ROTATE_PATH_01, str);
  if (ret < 0) {
    __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
    return ret;
  }

  sprintf(str, "%d", (arg.ch_rotate >> 4) & 0x0f);
  __LOG(LOG_INFO, "[%s][%s:%d] Writing %s to %s", LOG_KEY, _FILE_, __LINE__,
        str, DEFAULT_ROTATE_PATH_23);
  ret = safe_write_file(DEFAULT_ROTATE_PATH_23, str);
  if (ret < 0) {
    __LOG(LOG_CRIT, "[%s][%s:%d] ret:%d", LOG_KEY, _FILE_, __LINE__, ret);
    return ret;
  }

  return ret;
}

gint ParserClass::check_arg() {
  const gchar *ioModeStr[6] = {"auto",   "rw",     "mmap",
                               "useptr", "dmabuf", "dmabuf-import"};
  gint i = 0;

  if (g_strcmp0(arg.enc, ENC_H264) != 0 &&
      g_strcmp0(arg.enc, ENC_H265) != 0) {
    __LOG(LOG_ERR,
          "[%s][%s:%d] invalid enc:%s, fallback to %s (h264 or h265)",
          LOG_KEY, _FILE_, __LINE__, arg.enc ? arg.enc : "(null)",
          DEFAULT_ENC);
    g_free(arg.enc);
    arg.enc = g_strdup(DEFAULT_ENC);
    g_cfg_errors++;
  }

  const gboolean use_h265 = g_strcmp0(arg.enc, ENC_H265) == 0;
  const gint profile_default =
      use_h265 ? DEFAULT_H265_PROFILE : DEFAULT_H264_PROFILE;
  const gint profile_min = use_h265 ? MIN_H265_PROFILE : MIN_H264_PROFILE;
  const gint profile_max = use_h265 ? MAX_H265_PROFILE : MAX_H264_PROFILE;

  __LOG(LOG_NOTICE, "[%s][%s:%d] enc:%s", LOG_KEY, _FILE_, __LINE__,
        arg.enc);
  //__LOG(LOG_NOTICE, "[RTSP][%s:%d] 0 : %s, 1 : %s, 2 : %s", _FILE_, __LINE__,
  // test0, test1, test2);
  __LOG(LOG_NOTICE,
        "[%s][%s:%d] iomode:%s, test:%d, noFault:%d, delay:%d, conf_delay:%d, "
        "logLevel:%d, dbgLevel:%d, mntDir:%s, dotDir:%s, ",
        LOG_KEY, _FILE_, __LINE__, ioModeStr[arg.ioMode], arg.levelMode,
        arg.fault, arg.play_delay, arg.config_delay, arg.log_level,
        arg.dbg_level, arg.mntDir, arg.dotDir);

  __LOG(LOG_NOTICE,
        "[%s][%s:%d] oht_name:%s, duration:%d, width:%d, height:%d, "
        "csi1_fps:%d, csi2_fps:%d, wdt_timeout_l:%d, wdt_timeout_s:%d",
        LOG_KEY, _FILE_, __LINE__, arg.ohtName, arg.duration, arg.width,
        arg.height, arg.main_fps[CSI_1], arg.main_fps[CSI_2],
        arg.wdt_timeout_long, arg.wdt_timeout_short);

  __LOG(LOG_INFO,
        "[%s][%s:%d] v4l_map subdev(csi0=%d,csi1=%d) video(csi0=%d,csi1=%d)",
        LOG_KEY, _FILE_, __LINE__, arg.v4l_subdev_csi0, arg.v4l_subdev_csi1,
        arg.v4l_video_csi0, arg.v4l_video_csi1);

  __LOG(LOG_NOTICE,
        "[%s][%s:%d] chEn:0x%x, recEn:%d, rtspEn:%d, capEn:%d, audoEn:%d, "
        "dualEn:%d, inputEn:%d, overlayEn:%d tcpEn:%d, tcpPort:%d, ipc_en:%d, "
        "ipc_mid:%d",
        LOG_KEY, _FILE_, __LINE__, arg.ch_enable, arg.stream_en[STREAM_REC],
        arg.stream_en[STREAM_RTSP], arg.stream_en[STREAM_CAP], arg.audio_en,
        arg.dual_enc, arg.input_en, arg.overlay_en, arg.tcp_en, arg.tcp_port,
        arg.ipc_en, arg.ipc_mid);

  for (i = 0; i < MAX_CHANNEL; i++) {
    __LOG(LOG_NOTICE,
          "[%s][%s:%d] ch%d en:%s, vflip:%s, hflip:%s, bps:%d,%d, ae_on:%d, "
          "ae_gain:%d, exp_time:%d, led_flash:%s(wiper=%u delay=%u)",
          LOG_KEY, _FILE_, __LINE__, i, arg.cam[i].enable ? "true" : "false",
          arg.cam[i].vflip ? "true" : "false",
          arg.cam[i].hflip ? "true" : "false", arg.cam[i].bps[STREAM_REC],
          arg.cam[i].bps[STREAM_RTSP], arg.cam[i].ae_on, arg.cam[i].ae_gain,
          arg.cam[i].exp_time,
          arg.cam[i].led_flash_enable ? "true" : "false",
          arg.cam[i].led_flash_wiper, arg.cam[i].led_flash_delay);

    /*
     * bps sanity. Accept JSON noise (string, negative, out-of-range) without
     * aborting startup: log and fall back to compiled-in defaults.
     * In single-encoder mode (!dual_enc) the RTSP bitrate is irrelevant
     * because one encoder feeds both record and rtsp; we mirror rec bps
     * onto the rtsp slot so downstream code stays consistent.
     */
    if (arg.cam[i].bps[STREAM_REC] < 0) {
      __LOG(LOG_ERR,
            "[%s][%s:%d] ch%d invalid rec bps %d, fallback to %d (0=auto, or kbps >= 0)",
            LOG_KEY, _FILE_, __LINE__, i, arg.cam[i].bps[STREAM_REC],
            DEFAULT_RECORD_BITRATE);
      arg.cam[i].bps[STREAM_REC] = DEFAULT_RECORD_BITRATE;
    }
    if (arg.cam[i].bps[STREAM_REC] == BITRATE_AUTO)
      __LOG(LOG_NOTICE,
            "[%s][%s:%d] ch%d rec bps 0: encoder rate control is automatic (VBR)",
            LOG_KEY, _FILE_, __LINE__, i);

    if (arg.dual_enc) {
      if (arg.cam[i].bps[STREAM_RTSP] < 0) {
        __LOG(LOG_ERR,
              "[%s][%s:%d] ch%d invalid rtsp bps %d, fallback to %d (0=auto, or kbps >= 0)",
              LOG_KEY, _FILE_, __LINE__, i, arg.cam[i].bps[STREAM_RTSP],
              DEFAULT_RTSP_BITRATE);
        arg.cam[i].bps[STREAM_RTSP] = DEFAULT_RTSP_BITRATE;
      }
    } else {
      if (arg.cam[i].bps[STREAM_RTSP] != arg.cam[i].bps[STREAM_REC]) {
        __LOG(LOG_INFO,
              "[%s][%s:%d] ch%d single-enc: align rtsp bps %d -> %d (= rec)",
              LOG_KEY, _FILE_, __LINE__, i, arg.cam[i].bps[STREAM_RTSP],
              arg.cam[i].bps[STREAM_REC]);
        arg.cam[i].bps[STREAM_RTSP] = arg.cam[i].bps[STREAM_REC];
      }
    }

    /*
     * VPU encoder tuning sanity, same fall-back-to-default policy as bps.
     * Profile defaults and ranges depend on the selected codec.
     * qp_min/qp_max treat 0 as "unset" (the VPU wrapper only honours > 0), so
     * 0 is legal and means "leave the hardware default in place".
     * gop 0 is also legal but is a sentinel, resolved to fps right after the
     * range check so the encoder only ever sees a concrete interval.
     */
    enc_knob_sanity(&arg.cam[i].gop[STREAM_REC], MIN_GOP_SIZE, MAX_GOP_SIZE,
                    DEFAULT_GOP_SIZE, "gop", "rec", i);
    enc_profile_sanity(&arg.cam[i].profile[STREAM_REC], profile_min,
                       profile_max, profile_default, "rec", i);
    enc_knob_sanity(&arg.cam[i].quant[STREAM_REC], MIN_QUANT, MAX_QUANT,
                    DEFAULT_QUANT, "quant", "rec", i);
    enc_knob_sanity(&arg.cam[i].qp_min[STREAM_REC], MIN_QP, MAX_QP,
                    DEFAULT_QP_MIN, "qp_min", "rec", i);
    enc_knob_sanity(&arg.cam[i].qp_max[STREAM_REC], MIN_QP, MAX_QP,
                    DEFAULT_QP_MAX, "qp_max", "rec", i);
    enc_gop_resolve(&arg.cam[i].gop[STREAM_REC], arg.fps[STREAM_REC][i],
                    "rec", i);

    if (arg.dual_enc) {
      enc_knob_sanity(&arg.cam[i].gop[STREAM_RTSP], MIN_GOP_SIZE, MAX_GOP_SIZE,
                      DEFAULT_GOP_SIZE, "gop", "rtsp", i);
      enc_profile_sanity(&arg.cam[i].profile[STREAM_RTSP], profile_min,
                         profile_max, profile_default, "rtsp", i);
      enc_knob_sanity(&arg.cam[i].quant[STREAM_RTSP], MIN_QUANT, MAX_QUANT,
                      DEFAULT_QUANT, "quant", "rtsp", i);
      enc_knob_sanity(&arg.cam[i].qp_min[STREAM_RTSP], MIN_QP, MAX_QP,
                      DEFAULT_QP_MIN, "qp_min", "rtsp", i);
      enc_knob_sanity(&arg.cam[i].qp_max[STREAM_RTSP], MIN_QP, MAX_QP,
                      DEFAULT_QP_MAX, "qp_max", "rtsp", i);
      enc_gop_resolve(&arg.cam[i].gop[STREAM_RTSP], arg.fps[STREAM_RTSP][i],
                      "rtsp", i);
    } else {
      enc_knob_mirror(&arg.cam[i].gop[STREAM_RTSP], arg.cam[i].gop[STREAM_REC],
                      "gop", i);
      enc_knob_mirror(&arg.cam[i].profile[STREAM_RTSP],
                      arg.cam[i].profile[STREAM_REC], "profile", i);
      enc_knob_mirror(&arg.cam[i].quant[STREAM_RTSP],
                      arg.cam[i].quant[STREAM_REC], "quant", i);
      enc_knob_mirror(&arg.cam[i].qp_min[STREAM_RTSP],
                      arg.cam[i].qp_min[STREAM_REC], "qp_min", i);
      enc_knob_mirror(&arg.cam[i].qp_max[STREAM_RTSP],
                      arg.cam[i].qp_max[STREAM_REC], "qp_max", i);
    }

    /* An inverted window would be silently clamped deep inside the VPU
     * wrapper; reject it here where it is still explainable. Both zero
     * (= unset) is the normal case and must pass. */
    for (gint s = 0; s < 2; s++) {
      if (arg.cam[i].qp_min[s] > 0 && arg.cam[i].qp_max[s] > 0 &&
          arg.cam[i].qp_min[s] > arg.cam[i].qp_max[s]) {
        __LOG(LOG_ERR,
              "[%s][%s:%d] ch%d %s qp_min %d > qp_max %d, reset both to unset",
              LOG_KEY, _FILE_, __LINE__, i, s == STREAM_REC ? "rec" : "rtsp",
              arg.cam[i].qp_min[s], arg.cam[i].qp_max[s]);
        arg.cam[i].qp_min[s] = DEFAULT_QP_MIN;
        arg.cam[i].qp_max[s] = DEFAULT_QP_MAX;
      }
    }

    /* Effective values — logged after every clamp, sentinel resolution and
     * single-enc alignment, so this line is what the encoder actually gets. */
    __LOG(LOG_NOTICE,
          "[%s][%s:%d] ch%d gop:%d,%d profile:%d,%d quant:%d,%d "
          "qp_min:%d,%d qp_max:%d,%d",
          LOG_KEY, _FILE_, __LINE__, i, arg.cam[i].gop[STREAM_REC],
          arg.cam[i].gop[STREAM_RTSP], arg.cam[i].profile[STREAM_REC],
          arg.cam[i].profile[STREAM_RTSP], arg.cam[i].quant[STREAM_REC],
          arg.cam[i].quant[STREAM_RTSP], arg.cam[i].qp_min[STREAM_REC],
          arg.cam[i].qp_min[STREAM_RTSP], arg.cam[i].qp_max[STREAM_REC],
          arg.cam[i].qp_max[STREAM_RTSP]);
  }

  if (g_cfg_errors > 0)
    __LOG(LOG_ERR,
          "[%s][%s:%d] !!! %d config error(s) in edgeconf: bad values IGNORED, "
          "DEFAULTS used — check edgeconf !!!",
          LOG_KEY, _FILE_, __LINE__, g_cfg_errors);

  if (arg.stream_en[STREAM_REC]) {
    __LOG(LOG_NOTICE,
          "[%s][%s:%d] splitSec:%d, splitMargin:%d, splitMax:%d, muxer:%s ch0 "
          "fps:%d, ch1 fps:%d, ch2 fps:%d, ch3 fps:%d",
          LOG_KEY, _FILE_, __LINE__, arg.split_sec, arg.split_diff_msec,
          arg.split_max_msec, arg.muxer, arg.fps[STREAM_REC][0],
          arg.fps[STREAM_REC][1], arg.fps[STREAM_REC][2],
          arg.fps[STREAM_REC][3]);
  }

  if (arg.stream_en[STREAM_RTSP]) {
    __LOG(LOG_NOTICE,
          "[%s][%s:%d] rtspID:%s, rtspPW:%s, rtspPort:%s ch0 fps:%d, ch1 "
          "fps:%d, ch2 fps:%d, ch3 fps:%d",
          LOG_KEY, _FILE_, __LINE__, arg.rtsp_id, arg.rtsp_passwd,
          arg.rtsp_port, arg.fps[STREAM_RTSP][0], arg.fps[STREAM_RTSP][1],
          arg.fps[STREAM_RTSP][2], arg.fps[STREAM_RTSP][3]);

    __LOG(LOG_NOTICE,
          "[%s][%s:%d] rtsp_tune factory_latency_ms:%d appsink_max_buffers:%d "
          "factory_queue_max_buffers:%d bin_queue_max_time_ms:%d "
          "appsrc_max_bytes:%d",
          LOG_KEY, _FILE_, __LINE__, arg.rtsp_factory_latency_ms,
          arg.rtsp_appsink_max_buffers, arg.rtsp_factory_queue_max_buffers,
          arg.rtsp_bin_queue_max_time_ms, arg.rtsp_appsrc_max_bytes);
  }
  __LOG(LOG_NOTICE,
        "[%s][%s:%d] queue_tune main_src:%dms enc_src:%dms rec_sink:%dms cap_src:%dms",
        LOG_KEY, _FILE_, __LINE__, arg.queue_main_src_time_ms,
        arg.queue_enc_src_time_ms, arg.queue_rec_sink_time_ms,
        arg.queue_cap_src_time_ms);

  if (arg.stream_en[STREAM_CAP]) {
    if (arg.cap.queue_size <= 0) {
      __LOG(LOG_WARNING,
            "[%s][%s:%d] invalid cap queue size %d, using default %d", LOG_KEY,
            _FILE_, __LINE__, arg.cap.queue_size, DEFAULT_CAPTURE_QUEUE_SIZE);
      arg.cap.queue_size = DEFAULT_CAPTURE_QUEUE_SIZE;
    }

    // [instant-snapshot] only 0/1/2 are valid; anything else disables the feature
    if (arg.cap.instant < CAP_INSTANT_OFF || arg.cap.instant > CAP_INSTANT_COPY) {
      __LOG(LOG_WARNING,
            "[%s][%s:%d] invalid capinstant %d (expected 0/1/2), disabling", LOG_KEY,
            _FILE_, __LINE__, arg.cap.instant);
      arg.cap.instant = CAP_INSTANT_OFF;
    }

    // Use safe_mkdir_p instead of system("mkdir -p ...")
    gchar *cap_dir = NULL;
    if (cmdArg.cap.dir && cmdArg.cap.dir[0] == '/') {
      cap_dir = g_strdup(cmdArg.cap.dir);
    } else {
      cap_dir = g_strdup_printf("%s/%s", cmdArg.mntDir, cmdArg.cap.path);
    }
    if (safe_mkdir_p(cap_dir, 0755) < 0)
      __LOG(LOG_ERR, "[CFG][%s:%d] err mkdir: %s", __FILE__, __LINE__, cap_dir);
    g_free(cap_dir);

    __LOG(LOG_NOTICE,
          "[%s][%s:%d] capture ch0 fps:%d, ch1 fps:%d, ch2 fps:%d, ch3 fps:%d",
          LOG_KEY, _FILE_, __LINE__, arg.fps[STREAM_CAP][0],
          arg.fps[STREAM_CAP][1], arg.fps[STREAM_CAP][2],
          arg.fps[STREAM_CAP][3]);
    __LOG(LOG_NOTICE,
          "[%s][%s:%d] capEnc:%s, MaxCnt:%d, res_en:%d, dir:%s, path:%s, "
          "delay:%d, timeout:%d, padding:%d, quality:%d, queue_size:%d",
          LOG_KEY, _FILE_, __LINE__, arg.cap.encoder, arg.cap.maxCnt,
          arg.cap.res_en, arg.cap.dir ? arg.cap.dir : "(null)", arg.cap.path,
          arg.cap.delay, arg.cap.timeout, arg.cap.padding, arg.cap.quality,
          arg.cap.queue_size);
  }

  gint total_fps = 0;

  for (i = 0; i < MAX_CHANNEL; i++) {
    if (!arg.cam[i].enable)
      continue;

    if (arg.dual_enc) {
      if (arg.stream_en[STREAM_REC])
        total_fps += arg.fps[STREAM_REC][i];
      if (arg.stream_en[STREAM_RTSP])
        total_fps += arg.fps[STREAM_RTSP][i];
    } else {
      if (arg.stream_en[STREAM_REC])
        total_fps += arg.fps[STREAM_REC][i];
      else if (arg.stream_en[STREAM_RTSP])
        total_fps += arg.fps[STREAM_RTSP][i];
    }
  }

  total_fps += arg.stream_en[STREAM_CAP] * 0;
  total_fps += arg.audio_en * 0;

  if (arg.duration < 1) {
    __LOG(LOG_CRIT, "[%s][%s:%d] recording duration %d not supported", LOG_KEY,
          _FILE_, __LINE__, arg.duration);
    return -1;
  }

  if (arg.width == 1280 && arg.height == 720) {
    if (total_fps > MAX_FPS_HD) {
      __LOG(LOG_CRIT,
            "[%s][%s:%d] HD max fps over : total_fps(%d) > MAX_FPS_HD(%d)",
            LOG_KEY, _FILE_, __LINE__, total_fps, MAX_FPS_HD);
      return -1;
    }
  } else if (arg.width == 1920 && arg.height == 1080) {
    if (total_fps > MAX_FPS_FHD) {
      __LOG(LOG_CRIT,
            "[%s][%s:%d] FHD max fps over : total_fps(%d) > MAX_FPS_FHD(%d)",
            LOG_KEY, _FILE_, __LINE__, total_fps, MAX_FPS_FHD);
      return -1;
    }
  } else {
    __LOG(LOG_CRIT, "[%s][%s:%d] width %d not supported", LOG_KEY, _FILE_,
          __LINE__, arg.width);
    return -1;
  }

  __LOG(LOG_INFO, "[%s][%s:%d] total_fps : %d", LOG_KEY, _FILE_, __LINE__,
        total_fps);

  return 0;
}

static void capture_done_callback(guint8 ch, gint completed_count,
                                  gpointer user_data) {
  if (!cmdArg.cap.res_en)
    return;

  // user_data stores tx_id directly
  guint32 tx_id = GPOINTER_TO_UINT(user_data);

  CIPCInsance *ipcInstance = CIPCInsance::getInstance();

  TCfiRecvData _TCfiRecvData;
  memset(_TCfiRecvData.byte, 0, CFI_RECV_DATA_LEN);
  _TCfiRecvData.data.len = CFI_RECV_DATA_LEN;
  _TCfiRecvData.data.cmd_id = CFI_CAP_RES_CMD_ID;
  _TCfiRecvData.data.tx_id = tx_id;
  _TCfiRecvData.data.channel = 1 << ch;
  _TCfiRecvData.data.cap_cnt = completed_count;

  __LOG(LOG_INFO, "[%s][%s:%d] Async capture done ch%d tx:%d cnt:%d",
        CAP_LOG_KEY, _FILE_, __LINE__, ch, tx_id, completed_count);

  ipcInstance->sendData((char *)_TCfiRecvData.byte, CFI_RECV_DATA_LEN);
}

gint ParserClass::cfi_parser(gchar *buffer, gint len, gpointer data) {
  ThreadArgs *thraedArgs = (ThreadArgs *)data;
  // VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
  // RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
  // RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
  // MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
  CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg4);
  // CaptureBin *captureBin = (CaptureBin *)(data);
  // ThreadArgs *arg[2];
  // CIPCInsance* ipcInstance = CIPCInsance::getInstance();

  TCfiSendData _TCfiSendData;
  // TCfiRecvData _TCfiRecvData;
  guint i = 0;
  gint ret = 0;
  // guint32 chk_cnt = 0;
  guint8 ch_en = 0;
  guint16 capMaxCnt = 0;
  guint32 timeout_msec = 0;
  guint8 fps;

  // GThread *resThread[4];
  // memset(_TCfiSendData.byte, 0, CFI_SEND_DATA_LEN);

  if (len != CFI_SEND_DATA_LEN) {
    __LOG(LOG_ERR, "[%s][%s:%d] recv byte %d != %d", CAP_LOG_KEY, _FILE_,
          __LINE__, len, CFI_SEND_DATA_LEN);
    return -1;
  }

  memcpy(_TCfiSendData.byte, buffer, len);

  if (_TCfiSendData.data.len != len) {
    __LOG(LOG_ERR, "[%s][%s:%d] header len %d != %d", CAP_LOG_KEY, _FILE_,
          __LINE__, _TCfiSendData.data.len, len);
    return -1;
  }

  if (_TCfiSendData.data.cmd_id != CFI_CAP_REQ_CMD_ID &&
      _TCfiSendData.data.cmd_id != CTS_CAP_START_REQ_CMD_ID &&
      _TCfiSendData.data.cmd_id != CTS_CAP_STOP_REQ_CMD_ID) {
    __LOG(LOG_ERR, "[%s][%s:%d] header cmd_id 0x%x is invalid", CAP_LOG_KEY,
          _FILE_, __LINE__, _TCfiSendData.data.cmd_id);
    return -1;
  }

  //__LOG(LOG_INFO, "[CFI][%s:%d] prefix : %s", _FILE_, __LINE__,
  //_TCfiSendData.data.prefix);

  ch_en = _TCfiSendData.data.channel;
  capMaxCnt = _TCfiSendData.data.cap_cnt;

  // For lossless PNG continuous capture, keep requests bounded.
  // We allow completion to be late, but memory/CPU can explode for large
  // counts.
  if (cmdArg.cap.encoder && g_strcmp0(cmdArg.cap.encoder, "png") == 0) {
    const guint16 MAX_PNG_CAP_CNT = 20;
    if (capMaxCnt > MAX_PNG_CAP_CNT) {
      __LOG(LOG_WARNING, "[%s][%s:%d] png cap_cnt clamped: %d -> %d",
            CAP_LOG_KEY, _FILE_, __LINE__, capMaxCnt, MAX_PNG_CAP_CNT);
      capMaxCnt = MAX_PNG_CAP_CNT;
    }
  }
  // json_sub_object_get_value(cmdArg.json_file, JSON_CAM_OBJ_NAME,
  // JSON_CAP_OBJ_NAME, "delay", &cmdArg.cap.delay);
  __LOG(LOG_INFO, "[%s][%s:%d] ch:0x%x, tx:%d, cnt:%d, prefix:%s, delay:%d",
        CAP_LOG_KEY, _FILE_, __LINE__, ch_en, _TCfiSendData.data.tx_id,
        capMaxCnt, _TCfiSendData.data.prefix, cmdArg.cap.delay);

  g_usleep(1000 * cmdArg.cap.delay);

  for (i = 0; i < MAX_CHANNEL; i++) {
    if ((ch_en >> i & 0x1) != 0x01)
      continue;

    if (!(cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP])) {
      __LOG(LOG_ERR,
            "[%s][%s:%d] ch%d capture is not available because ch%d is disable",
            CAP_LOG_KEY, _FILE_, __LINE__, i, i);
      ch_en &= ~(1 << i);
      continue;
    }

    if (_TCfiSendData.data.cmd_id == CFI_CAP_REQ_CMD_ID) {
      fps = captureBin[i].getFPS();
      if (fps <= 0) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d has invalid FPS=%d", CAP_LOG_KEY,
              _FILE_, __LINE__, i, fps);
        fps = 1; // fallback
      }
      if (cmdArg.cap.timeout <= 100) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d has invalid timeout=%d", CAP_LOG_KEY,
              _FILE_, __LINE__, i, cmdArg.cap.timeout);
        cmdArg.cap.timeout = 200; // fallback
      }

      // Calculate timeout for async mode
      timeout_msec = (capMaxCnt * 1000) / fps;
      guint32 processing_overhead = capMaxCnt * 50;
      timeout_msec = timeout_msec + processing_overhead + cmdArg.cap.timeout;

      // PNG encoding is CPU-heavy (lossless compression). The generic timeout
      // calculation above (based on fps) severely underestimates real encode
      // time, which can cause partial output (e.g., 10/30) before timeout.
      if (cmdArg.cap.encoder && g_strcmp0(cmdArg.cap.encoder, "png") == 0) {
        const guint32 base_min_ms = 10000; // negotiation + first-frame latency
        const guint32 per_frame_ms = 2500; // conservative budget per PNG frame
        guint32 scaled = 2000 + (guint32)capMaxCnt * per_frame_ms;
        if (scaled < base_min_ms)
          scaled = base_min_ms;
        if (timeout_msec < scaled)
          timeout_msec = scaled;

        __LOG(LOG_NOTICE,
              "[%s][%s:%d] ch%d png timeout scaled: cnt:%d fps:%d -> "
              "timeout:%u ms",
              CAP_LOG_KEY, _FILE_, __LINE__, i, capMaxCnt, fps, timeout_msec);
      }

      // Register callback once (safe to call multiple times)
      captureBin[i].setCompleteCallback(capture_done_callback, NULL);

      gpointer callback_data = NULL;
      if (cmdArg.cap.res_en) {
        // Pass tx_id directly as pointer
        callback_data = GUINT_TO_POINTER(_TCfiSendData.data.tx_id);
      }

      // Queue the request with timeout
      captureBin[i].addCaptureRequest(capMaxCnt,
                                      (gchar *)_TCfiSendData.data.prefix,
                                      callback_data, 1, (gint)timeout_msec);
    } else if (_TCfiSendData.data.cmd_id == CTS_CAP_START_REQ_CMD_ID) {
      // Map to request queue with mode 2
      captureBin[i].addCaptureRequest(
          capMaxCnt, (gchar *)_TCfiSendData.data.prefix, NULL, 2, G_MAXINT);
    } else if (_TCfiSendData.data.cmd_id == CTS_CAP_STOP_REQ_CMD_ID) {
      captureBin[i].stopCapture();
    }
  }

  return ret;
}

gint ParserClass::cmd_parser(gchar *buffer, gint len, gpointer data) {
#define SPLIT_CHAR " "

  gint i, key, key1, key2;
  gchar *token = NULL;
  const gchar *stateStr[] = {"PENDING", "NULL", "READY", "PAUSED", "PLAYING"};
  // const gchar *stateChangeReturnStr[] = {"GST_STATE_CHANGE_FAILURE",
  // "GST_STATE_CHANGE_SUCCESS", "GST_STATE_CHANGE_ASYNC",
  // "GST_STATE_CHANGE_NO_PREROLL"};
  ThreadArgs *thraedArgs = (ThreadArgs *)data;
  VideoBin *videoBin = (VideoBin *)(thraedArgs->arg0);
  RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
  RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
  MuxSinkBin *muxSinkBin = (MuxSinkBin *)(thraedArgs->arg3);
  CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg4);
  EncoderBin *encoderBin = (EncoderBin *)(thraedArgs->arg5);
  const gboolean single_enc = !cmdArg.dual_enc;
  const gboolean any_enc_stream =
      (cmdArg.stream_en[STREAM_REC] || cmdArg.stream_en[STREAM_RTSP]);
  const gboolean rec_ctrl_stream =
      (cmdArg.stream_en[STREAM_REC] ||
       (single_enc && cmdArg.stream_en[STREAM_RTSP]));
  GstState state;
  // GstStateChangeReturn stateRet;
  // GstPadLinkReturn linkRet;

  // gint len = strlen(buffer);
  if (len >= 0) {
    buffer[len] = '\0';
  }
  g_print("Input: %s\n", buffer);
  //__LOG(LOG_NOTICE, "[TCP][%s:%d] Input: %s", _FILE_, __LINE__, buffer);

  token = strtok(buffer, " ");

#if 1
  for (i = 0; i < len; i++) {
    if (buffer[i] == '\0' || buffer[i] == '\n')
      buffer[i] = SPLIT_CHAR[0];
  }
#endif

  token = strtok(buffer, SPLIT_CHAR);

#if 1
  if (compareBuf(token, "get", 3)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "bps", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].getBitrate();
            } else {
              recordBin[i].getBitrate();
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP bitrate is shared. use 'get bps rec'\n");
          return -1;
        }
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getBitrate();
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "fps", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].getFps();
            } else {
              recordBin[i].getFps();
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP fps is shared. use 'get fps rec' or 'get rate'\n");
          return -1;
        }
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getFps();
      } else if (compareBuf(token, "cam", 3)) {
        g_print("cam fps: csi0=%d csi1=%d (v4l2 subdev)\n",
                cmdArg.main_fps[CSI_1], cmdArg.main_fps[CSI_2]);
      } else if (compareBuf(token, "video", 5)) {
        for (i = 0; i < MAX_VIDEO_SRC; i++)
          if (videoBin[i].be.bin != NULL) {
            GstCaps *caps = NULL;
            g_object_get(videoBin[i].be.capsfilter, "caps", &caps, NULL);
            if (caps) {
              gchar *caps_str = gst_caps_to_string(caps);
              g_print("video csi%d caps: %s\n", i, caps_str);
              g_free(caps_str);
              gst_caps_unref(caps);
            }
          }
      } else if (compareBuf(token, "rate", 4)) {
        if (!cmdArg.videorate_en) {
          g_print("videorate disabled (use --evrate 1)\n");
        } else {
          for (i = 0; i < MAX_CHANNEL; i++) {
            if (!cmdArg.cam[i].enable)
              continue;
            if (!cmdArg.dual_enc && encoderBin && encoderBin[i].re.rate) {
              gint max_rate;
              g_object_get(encoderBin[i].re.rate, "max-rate", &max_rate, NULL);
              g_print("enc ch%d videorate max-rate: %d\n", i, max_rate);
            }
          }
        }
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        // for (i = 0; i < MAX_CHANNEL; i++)
        // if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC])
        // recordBin[i].getFps();
      } else if (compareBuf(token, "rtsp", 4)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getCaps();
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "rotate", 6)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].getRotation();
            } else {
              recordBin[i].getRotation();
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP rotate is shared. use 'get rotate rec'\n");
          return -1;
        }
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getRotation();
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "state", 5)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream) {
                state = encoderBin[i].getState();
                g_print("enc ch%d state : %s\n", i, stateStr[state]);
              }
            } else {
              state = recordBin[i].getState();
              g_print("rec ch%d state : %s\n", i, stateStr[state]);
            }
          }
        }
      } else if (compareBuf(token, "rtsp", 4)) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            state = rtspServerBin[i].getState();
            g_print("rtsp ch%d state : %s\n", i, stateStr[state]);
          }
        }
      } else if (compareBuf(token, "cap", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            state = captureBin[i].getState();
            g_print("capture ch%d state : %s\n", i, stateStr[state]);
          }
        }
      } else if (compareBuf(token, "pipe", 4)) {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "0", 1)) {
          gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
          g_print("pipe state : %s\n", stateStr[state]);
        } else if (compareBuf(token, "1", 1)) {
          // gst_element_get_state(pipeline2, &state, NULL,
          // GST_CLOCK_TIME_NONE);
          g_print("pipe1 state : %s\n", stateStr[state]);
        } else
          g_print("wrong cmd!\n");
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "iomode", 6)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "0", 1)) {
        videoBin[0].getIoMode();
      } else if (compareBuf(token, "1", 1)) {
        videoBin[1].getIoMode();
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "gop", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].getGop();
            } else {
              recordBin[i].getGop();
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP gop is shared. use 'get gop rec'\n");
          return -1;
        }
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getGop();
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "key", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].getKeyframe();
            } else {
              recordBin[i].getKeyframe();
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP keyframe is shared. use 'get key rec'\n");
          return -1;
        }
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getKeyframe();
      } else
        g_print("wrong cmd!\n");
    } else
      g_print("wrong cmd!\n");
  } // get
#endif
#if 1
  else if (compareBuf(token, "set", 3)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "bps", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        i = charArrayToInt(token);
        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        /* Same acceptance rule as the edgeconf path in check_arg(). */
        if (key < 0) {
          g_print("bps %d not supported (0=auto/VBR, or kbps >= 0)\n", key);
          return -1;
        }

        if (cmdArg.cam[i].enable && rec_ctrl_stream) {
          if (single_enc) {
            if (any_enc_stream)
              encoderBin[i].setBitrate(key);
            /* One encoder feeds both paths, so keep the rtsp slot aligned the
             * way check_arg() does at startup. */
            cmdArg.cam[i].bps[STREAM_REC] = key;
            cmdArg.cam[i].bps[STREAM_RTSP] = key;
          } else {
            recordBin[i].setBitrate(key);
            cmdArg.cam[i].bps[STREAM_REC] = key;
          }
        }
        // for (i = 0; i < MAX_CHANNEL; i++)
        // if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC])
        // recordBin[i].setBitrate(key);
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP bitrate cannot be set separately. use 'set bps rec <ch> <bps>'\n");
          return -1;
        }
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        /* Same acceptance rule as the edgeconf path in check_arg(). */
        if (key < 0) {
          g_print("bps %d not supported (0=auto/VBR, or kbps >= 0)\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            rtspServerBin[i].setBitrate(key);
            cmdArg.cam[i].bps[STREAM_RTSP] = key;
          }
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "fps", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 99) {
          g_print("fps %d not supported\n", key);
          return -1;
        }

        if (cmdArg.cam[i].enable && rec_ctrl_stream) {
          if (single_enc) {
            if (any_enc_stream)
              encoderBin[i].setFps(key);
            cmdArg.fps[STREAM_REC][i] = key;
            cmdArg.fps[STREAM_RTSP][i] = key;
          } else {
            recordBin[i].setFps(key);
          }
        }

        // for (i = 0; i < MAX_CHANNEL; i++)
        // if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC])
        // recordBin[i].setFps(key);
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP fps cannot be set separately. use 'set fps rec <ch> <fps>' or 'set rate'\n");
          return -1;
        }
        token = strtok(NULL, SPLIT_CHAR);
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 99) {
          g_print("fps %d not supported\n", key);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
          rtspServerBin[i].setFps(key);
        // for (i = 0; i < MAX_CHANNEL; i++)
        // if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
        // rtspServerBin[i].setFps(key);
      } else if (compareBuf(token, "cam", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 1 || key > 120) {
          g_print("fps %d not supported (valid: 1~120)\n", key);
          return -1;
        }

        set_v4l2_subdev_fps(0, key);
        if (cmdArg.v4l_subdev_csi1 != cmdArg.v4l_subdev_csi0)
          set_v4l2_subdev_fps(1, key);

        cmdArg.main_fps[CSI_1] = key;
        cmdArg.main_fps[CSI_2] = key;

        __LOG(LOG_NOTICE, "[GST][%s:%d] set cam fps=%d (v4l2 subdev FSYNC)", _FILE_, __LINE__, key);
      } else if (compareBuf(token, "video", 5)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 1 || key > 120) {
          g_print("fps %d not supported (valid: 1~120)\n", key);
          return -1;
        }

        for (i = 0; i < MAX_VIDEO_SRC; i++) {
          if (videoBin[i].be.bin != NULL)
            videoBin[i].setFps(key);
        }

        __LOG(LOG_NOTICE, "[GST][%s:%d] set video fps=%d (videoBin capsfilter)", _FILE_, __LINE__, key);
      } else if (compareBuf(token, "rate", 4)) {
        if (!cmdArg.videorate_en) {
          g_print("videorate disabled (use --evrate 1)\n");
          return -1;
        }

        token = strtok(NULL, SPLIT_CHAR);
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 1 || key > 120) {
          g_print("fps %d not supported (valid: 1~120)\n", key);
          return -1;
        }

        if (!cmdArg.cam[i].enable) {
          g_print("channel %d not enabled\n", i);
          return -1;
        }

        if (cmdArg.dual_enc) {
          if (cmdArg.stream_en[STREAM_REC])
            recordBin[i].setFps(key);
          if (cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].setFps(key);
        } else {
          if (encoderBin && encoderBin[i].re.rate)
            g_object_set(encoderBin[i].re.rate, "max-rate", (gint)key, NULL);
          else
            g_print("ch%d videorate not available\n", i);
        }

        __LOG(LOG_NOTICE, "[GST][%s:%d] set rate ch%d fps=%d (videorate max-rate)", _FILE_, __LINE__, i, key);
      } else if (compareBuf(token, "main", 4)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 1 || key > 120) {
          g_print("fps %d not supported (valid: 1~120)\n", key);
          return -1;
        }

        /* videorate 가 없으면 인코더단이 소스 레이트 변경을 흡수하지 못한다.
         * 소스와 인코더 capsfilter 를 어느 순서로 바꾸든 중간에 caps 불일치
         * 구간이 생겨 v4l2src 까지 재협상이 전파되고 채널이 정지한다. */
        if (!cmdArg.videorate_en) {
          g_print("videorate disabled (use --evrate 1). use 'set fps cam %d' instead\n", key);
          return -1;
        }

        /* 1) Driver FSYNC change via v4l2 subdev ioctl */
        set_v4l2_subdev_fps(0, key);
        if (cmdArg.v4l_subdev_csi1 != cmdArg.v4l_subdev_csi0)
          set_v4l2_subdev_fps(1, key);

        cmdArg.main_fps[CSI_1] = key;
        cmdArg.main_fps[CSI_2] = key;

        /* 2) Update videoBin capsfilter framerate */
        for (i = 0; i < MAX_VIDEO_SRC; i++) {
          if (videoBin[i].be.bin != NULL)
            videoBin[i].setFps(key);
        }

        /* 3) Update encoder framerate (only when videorate is enabled).
         * 인코더 capsfilter 가 videorate 의 출력 레이트를 정하므로, 여기를
         * 갱신하지 않으면 소스만 20fps 로 내려도 videorate 가 기존 60fps 로
         * 프레임을 복제한다. videorate 하류라 재협상은 안전하다. */
        if (cmdArg.videorate_en) {
          for (i = 0; i < MAX_CHANNEL; i++) {
            if (!cmdArg.cam[i].enable)
              continue;

            /* gop 은 프레임 수라 fps 만 바꾸면 IDR 시간 간격이 그대로 늘어난다
             * (120 프레임: 60fps 에서 2초, 20fps 에서 6초). 플레이어가 화면을
             * 복구하려면 IDR 을 기다려야 하므로 시간 간격을 유지하도록
             * fps 비율로 재조정한다. cmdArg.fps[] 갱신 전에 계산해야 한다. */
            gint prev_fps = cmdArg.fps[STREAM_REC][i];
            gint new_gop = cmdArg.cam[i].gop[STREAM_REC];
            if (prev_fps > 0 && new_gop > 0) {
              new_gop = (gint)(((gint64)new_gop * key + prev_fps / 2) / prev_fps);
              if (new_gop < 1)
                new_gop = 1;
              if (new_gop > MAX_GOP_SIZE)
                new_gop = MAX_GOP_SIZE;
            }

            if (cmdArg.dual_enc) {
              if (cmdArg.stream_en[STREAM_REC]) {
                recordBin[i].setFps(key);
                recordBin[i].setGop(new_gop);
                recordBin[i].setBitrate(cmdArg.cam[i].bps[STREAM_REC]);
              }
              if (cmdArg.stream_en[STREAM_RTSP]) {
                rtspServerBin[i].setFps(key);
                rtspServerBin[i].setGop(new_gop);
                rtspServerBin[i].setBitrate(cmdArg.cam[i].bps[STREAM_RTSP]);
              }
            } else {
              if (encoderBin) {
                encoderBin[i].setFps(key);
                if (encoderBin[i].re.rate)
                  g_object_set(encoderBin[i].re.rate, "max-rate", (gint)key, NULL);
                /* setGop/setBitrate 는 re.enc NULL 가드가 없다 */
                if (encoderBin[i].re.enc) {
                  encoderBin[i].setGop(new_gop);
                  /* caps 변경 후 rate control 이 옛 framerate 기준으로 남아
                   * 비트레이트가 폭주하는 것을 막기 위해 재적용한다 */
                  encoderBin[i].setBitrate(cmdArg.cam[i].bps[STREAM_REC]);
                }
                /* 새 파라미터셋을 즉시 내보내 플레이어 복구를 앞당기고,
                 * 채널 간 IDR 위상을 맞춰 split skew 도 줄인다 */
                encoderBin[i].forceKeyframe();
              }
            }
            cmdArg.cam[i].gop[STREAM_REC] = new_gop;
            cmdArg.cam[i].gop[STREAM_RTSP] = new_gop;
            cmdArg.fps[STREAM_REC][i] = key;
            cmdArg.fps[STREAM_RTSP][i] = key;
          }
        }

        __LOG(LOG_NOTICE, "[GST][%s:%d] set main fps=%d (cam + video + rate + gop/idr)", _FILE_, __LINE__, key);
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "rotate", 6)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 5) {
          g_print("rotation %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].setRotation(key);
            } else {
              recordBin[i].setRotation(key);
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP rotation cannot be set separately. use 'set rotate rec <val>'\n");
          return -1;
        }
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 5) {
          g_print("rotation %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].setRotation(key);
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "state", 5)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, "\0");
        key = charArrayToInt(token);

        if (key < 0 || key > 4) {
          g_print("state %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (!any_enc_stream)
                continue;

              g_print("enc ch%d state : %s\n", i,
                      stateStr[encoderBin[i].getState()]);
              if (encoderBin[i].setState((GstState)key) ==
                  GST_STATE_CHANGE_FAILURE) {
                g_print("enc %s state change error\n", stateStr[key]);
              } else {
                g_print("enc ch%d state : %s\n", i,
                        stateStr[encoderBin[i].getState()]);
              }
            } else {
              g_print("rec ch%d state : %s\n", i,
                      stateStr[recordBin[i].getState()]);
              if (recordBin[i].setState((GstState)key) ==
                  GST_STATE_CHANGE_FAILURE) {
                g_print("rec %s state change error\n", stateStr[key]);
              } else {
                g_print("rec ch%d state : %s\n", i,
                        stateStr[recordBin[i].getState()]);
              }
            }
          }
        }

        g_print("rec state : %s\n", stateStr[key]);
      } else if (compareBuf(token, "rtsp", 4)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 4) {
          g_print("state %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            g_print("rtsp ch%d state : %s\n", i,
                    stateStr[rtspServerBin[i].getState()]);
            if (rtspServerBin[i].setState((GstState)key) ==
                GST_STATE_CHANGE_FAILURE) {
              g_print("rtsp %s state change error\n", stateStr[key]);
            } else {
              g_print("rtsp ch%d state : %s\n", i,
                      stateStr[rtspServerBin[i].getState()]);
            }
          }
        }

        g_print("rtsp state : %s\n", stateStr[state]);
      } else if (compareBuf(token, "audio", 5)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);
        i = 4;

        if (key < 0 || key > 4) {
          g_print("state %d not supported\n", key);
          return -1;
        }

        if (cmdArg.audio_en) {
          g_print("rtsp ch%d state : %s\n", i,
                  stateStr[rtspServerBin[i].getState()]);
          if (rtspServerBin[i].setState((GstState)key) ==
              GST_STATE_CHANGE_FAILURE) {
            g_print("rtsp %s state change error\n", stateStr[key]);
          } else {
            g_print("rtsp ch%d state : %s\n", i,
                    stateStr[rtspServerBin[i].getState()]);
          }
        }
      } else if (compareBuf(token, "cap", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 4) {
          g_print("state %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            g_print("before capture ch%d state : %s\n", i,
                    stateStr[captureBin[i].getState()]);
            g_print("capture ch%d state change ret : %d\n", i,
                    captureBin[i].setState((GstState)key));
            g_print("after capture ch%d state : %s\n", i,
                    stateStr[captureBin[i].getState()]);
          }
        }
        // g_print("capture state : %s\n", stateStr[state]);
      } else if (compareBuf(token, "pipe", 4)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 4) {
          g_print("state %d not supported\n", key);
          return -1;
        }
        // g_print("before pipe state : %s\n", i, gst_element_get_state());
        gst_element_set_state(pipeline, (GstState)key);
        g_print("after pipe state : %s\n", stateStr[key]);
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "iomode", 6)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "0", 1)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 5) {
          g_print("ioMode %d not supported\n", key);
          return -1;
        }

        videoBin[0].setIoMode(key);
      } else if (compareBuf(token, "1", 1)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 5) {
          g_print("ioMode %d not supported\n", key);
          return -1;
        }

        videoBin[1].setIoMode(key);
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "gop", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        /* Same acceptance rule as the edgeconf path in check_arg(). 0 is the
         * "follow fps" sentinel and is resolved per channel below — it must
         * not reach the encoder raw. */
        if (key < MIN_GOP_SIZE || key > MAX_GOP_SIZE) {
          g_print("gop %d not supported (0=fps(1s), or %d..%d)\n", key,
                  MIN_GOP_SIZE + 1, MAX_GOP_SIZE);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            /* Resolve per channel: fps is per channel, so one "0" can land on
             * different intervals across channels. */
            gint gop = key;
            enc_gop_resolve(&gop, cmdArg.fps[STREAM_REC][i], "rec", i);
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].setGop(gop);
              cmdArg.cam[i].gop[STREAM_REC] = gop;
              cmdArg.cam[i].gop[STREAM_RTSP] = gop;
            } else {
              recordBin[i].setGop(gop);
              cmdArg.cam[i].gop[STREAM_REC] = gop;
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP gop cannot be set separately. use 'set gop rec <val>'\n");
          return -1;
        }
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        /* Same acceptance rule as the edgeconf path in check_arg(). 0 is the
         * "follow fps" sentinel and is resolved per channel below — it must
         * not reach the encoder raw. */
        if (key < MIN_GOP_SIZE || key > MAX_GOP_SIZE) {
          g_print("gop %d not supported (0=fps(1s), or %d..%d)\n", key,
                  MIN_GOP_SIZE + 1, MAX_GOP_SIZE);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            gint gop = key;
            enc_gop_resolve(&gop, cmdArg.fps[STREAM_RTSP][i], "rtsp", i);
            rtspServerBin[i].setGop(gop);
            cmdArg.cam[i].gop[STREAM_RTSP] = gop;
          }
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "key", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rec", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 1) {
          g_print("key %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && rec_ctrl_stream) {
            if (single_enc) {
              if (any_enc_stream)
                encoderBin[i].setkeyframe(key);
            } else {
              recordBin[i].setkeyframe(key);
            }
          }
      } else if (compareBuf(token, "rtsp", 4)) {
        if (single_enc) {
          g_print(
              "single-encoder mode: RTSP keyframe cannot be set separately. use 'set key rec <val>'\n");
          return -1;
        }
        token = strtok(NULL, SPLIT_CHAR);
        key = charArrayToInt(token);

        if (key < 0 || key > 1) {
          g_print("key %d not supported\n", key);
          return -1;
        }

        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].setkeyframe(key);
      } else
        g_print("wrong cmd!\n");
    } else if (compareBuf(token, "dbg", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (compareBuf(token, "rtsp", 4)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].setTimeStampDebug();

        if (cmdArg.audio_en)
          rtspServerBin[4].setTimeStampDebug();
      } else if (compareBuf(token, "rec", 3)) {

      } else if (compareBuf(token, "cap", 3)) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP])
            captureBin[i].setTimeStampDebug();
      }
    } else
      g_print("wrong cmd!\n");
  } // set
  else if (compareBuf(token, "start", 5)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      key = charArrayToInt(token);

      if (key < 0 || key > 3) {
        g_print("channel %d not supported\n", i);
        return -1;
      }

      token = strtok(NULL, SPLIT_CHAR);
      key1 = charArrayToInt(token);

      if (key1 < 0 || key1 > 2) {
        g_print("cap mode %d not supported\n", key1);
        return -1;
      }

      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        key2 = 0;
      } else {
        key2 = charArrayToInt(token);
        if (key2 < 0) {
          g_print("cap max cnt %d not supported\n", key2);
          return -1;
        }
      }

      if (key1 == 0) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (single_enc) {
              GstPad *rec_src = encoderBin[i].getBinRecSrcPad();
              GstPad *rec_sink = muxSinkBin[i].getBinQueuePad();
              if (rec_src == NULL || rec_sink == NULL) {
                g_print("ch%d rec pad not ready!\n", i);
              } else if (gst_pad_is_linked(rec_src) == TRUE) {
                if (gst_pad_unlink(rec_src, rec_sink)) {
                  g_print("ch%d rec unlink ok!\n", i);
                } else
                  g_print("ch%d rec unlink err!\n", i);
              } else
                g_print("ch%d rec already unlinked!\n", i);
            } else {
              if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) ==
                  TRUE) {
                if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i),
                                   recordBin[i].getBinSinkPad())) {
                  g_print("ch%d rec unlink ok!\n", i);
                } else
                  g_print("ch%d rec unlink err!\n", i);
              } else
                g_print("ch%d rec already unlinked!\n", i);
            }
          }
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (single_enc) {
              GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
              GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
              if (rtsp_src == NULL || rtsp_sink == NULL) {
                g_print("ch%d rtsp pad not ready!\n", i);
              } else if (gst_pad_is_linked(rtsp_src) == TRUE) {
                if (gst_pad_unlink(rtsp_src, rtsp_sink)) {
                  g_print("ch%d rtsp unlink ok!\n", i);
                } else
                  g_print("ch%d rtsp unlink err!\n", i);
              } else
                g_print("ch%d rtsp already unlinked!\n", i);
            } else {
              if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) ==
                  TRUE) {
                if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i),
                                   rtspServerBin[i].getBinSinkPad())) {
                  g_print("ch%d rtsp unlink ok!\n", i);
                } else
                  g_print("ch%d rtsp unlink err!\n", i);
              } else
                g_print("ch%d rtsp already unlinked!\n", i);
            }
          }
        }
      }

      if (cmdArg.cam[key].enable && cmdArg.stream_en[STREAM_CAP]) {
        if (key1 == 2) {
          if (!captureBin[key].init(key))
            g_print("ch%d captrueBin init failed\n", key);

          if (captureBin[key].addBinToPipe(pipeline))
            g_print("ch%d capture bin add\n", key);
          else
            g_print("ch%d capture bin add error\n", key);
          // g_usleep(10000);

          if (gst_pad_is_linked(videoBin[key / 2].getBinCaptureSrcPad(key)) !=
              TRUE) {
            // g_print("ch%d capture not linked\n", key);
            if (gst_pad_link(videoBin[key / 2].getBinCaptureSrcPad(key),
                             captureBin[key].getBinSinkPad()) !=
                GST_PAD_LINK_OK) {
              g_print("ch%d capture link error!\n", key);
              // captureBin[i].removeBinToPipe(pipeline);
              return -1;
            } else {
              g_print("ch%d capture link ok!\n", key);
              // gst_element_sync_state_with_parent(captureBin[i].be.bin);
              // gst_element_set_state(pipeline, GST_STATE_PLAYING);
            }
          } else {
            g_print("ch%d capture already linked!\n", key);
            // gst_element_sync_state_with_parent(captureBin[i].be.bin);
            // gst_element_set_state(pipeline, GST_STATE_PLAYING);
          }

          for (i = 0; i < 500; i++) {
            g_print("pipeline playing\n");
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            // g_print("ch%d captrue state sync\n", key);
            // gst_element_sync_state_with_parent(captureBin[key].be.bin);
            // g_usleep(1000);
            state = captureBin[key].getState();
            if (state == GST_STATE_PLAYING)
              break;
            else
              g_print("state : %d\n", state);

            g_usleep(1000);
          }
        }

        captureBin[key].setFilePath(NULL);

        if (key1 == 0) {
          key2 = cmdArg.fps[STREAM_CAP][key] * 60;
        } else if (key1 == 1) {
          if (key2 == 0)
            key2 = cmdArg.cap.maxCnt;
        } else if (key1 == 2) {
          if (key2 == 0)
            key2 = cmdArg.cap.maxCnt;
        }

        g_print("ch:%d, fps:%d, mode : %d, max_cnt : %d\n", key,
                cmdArg.fps[STREAM_CAP][key], key1, key2);
        captureBin[key].startCapture(key2);

        // captureBin[i].setState(GST_STATE_READY);
        if (key1 == 2) {
#if 0
                    //g_usleep(100000);
                    time = (1000/cmdArg.fps[STREAM_CAP][i])*(key2+1);
                    g_print("sleep time : %dmsec\n", time);
                    g_usleep(time*1000);
#endif
          for (i = 0; i < 500; i++) {
            if (captureBin[key].getCaptureCnt() >= key2)
              break;

            g_usleep(10000);
          }

          __LOG(LOG_NOTICE, "[GST][%s:%d] capture end", _FILE_, __LINE__);

          if (gst_pad_unlink(videoBin[key / 2].getBinCaptureSrcPad(key),
                             captureBin[key].getBinSinkPad())) {
            g_print("ch%d capture unlink ok!\n", key);
            state = captureBin[key].getState();
            g_print("state : %d\n", state);
            for (i = 0; i < 500; i++) {
              captureBin[key].setState(GST_STATE_NULL);
              state = captureBin[key].getState();
              g_print("state : %d\n", state);
              if (state == GST_STATE_NULL) {
                if (captureBin[key].removeBinToPipe(pipeline))
                  g_print("ch%d capture bin remove\n", key);
                else
                  g_print("ch%d capture bin remove error\n", key);

                break;
              }
              g_usleep(10000);
            }
          } else
            g_print("ch%d capture unlink err!\n", key);

          // captureBin[i].removeBinToPipe(pipeline);
        }
      }
    } else if (compareBuf(token, "rec", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (token == NULL) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        for (i = 0; i < MAX_CHANNEL; i++) {

          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (single_enc) {
              gst_element_sync_state_with_parent(encoderBin[i].re.bin);

              while (1) {
                state = encoderBin[i].getState();
                if (state == GST_STATE_PLAYING)
                  break;
                else
                  g_print("ch%d enc state : %d\n", i, state);

                g_usleep(1000);
              }
            } else {
              gst_element_sync_state_with_parent(recordBin[i].re.bin);

              while (1) {
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
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
          if (single_enc) {
            gst_element_sync_state_with_parent(encoderBin[i].re.bin);

            while (1) {
              state = encoderBin[i].getState();
              if (state == GST_STATE_PLAYING)
                break;
              else
                g_print("ch%d enc state : %d\n", i, state);

              g_usleep(1000);
            }
          } else {
            gst_element_sync_state_with_parent(recordBin[i].re.bin);

            while (1) {
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
    } else if (compareBuf(token, "rtsp", 4)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (token == NULL) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (single_enc)
              gst_element_sync_state_with_parent(encoderBin[i].re.bin);
            gst_element_sync_state_with_parent(rtspServerBin[i].re.bin);

            while (1) {
              state = rtspServerBin[i].getState();
              if (state == GST_STATE_PLAYING)
                break;
              else
                g_print("ch%d rtsp state : %d\n", i, state);

              g_usleep(1000);
            }
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
          if (single_enc)
            gst_element_sync_state_with_parent(encoderBin[i].re.bin);
          gst_element_sync_state_with_parent(rtspServerBin[i].re.bin);

          while (1) {
            state = rtspServerBin[i].getState();
            if (state == GST_STATE_PLAYING)
              break;
            else
              g_print("ch%d rtsp state : %d\n", i, state);

            g_usleep(1000);
          }
        }
      }
    } else if (compareBuf(token, "split", 5)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        g_print("split now\n");
        for (i = 0; i < MAX_CHANNEL; i++)
          if (muxSinkBin[i].getBinVideoSinkPad())
            muxSinkBin[i].splitNow(NULL, FALSE);
      } else {
        key = charArrayToInt(token);

        if (key < 0 || key > 59) {
          g_print("set_sec %d not supported\n", key);
          return -1;
        }
        g_print("split set_sec : %d\n", key);
        cmdArg.split_sec = key;
      }
    } else
      g_print("wrong cmd!\n");
  } // start
  else if (compareBuf(token, "link", 4)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        // gst_element_set_state(pipeline, GST_STATE_PAUSED);
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) !=
                TRUE) {
              if (gst_pad_link(videoBin[i / 2].getBinCaptureSrcPad(i),
                               captureBin[i].getBinSinkPad()) !=
                  GST_PAD_LINK_OK) {
                g_print("ch%d capture link error!\n", i);
              } else {
                captureBin[i].stopCapture();
                g_print("ch%d capture link ok!\n", i);
              }
            } else
              g_print("ch%d capture already link!\n", i);
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
          if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) !=
              TRUE) {
            if (gst_pad_link(videoBin[i / 2].getBinCaptureSrcPad(i),
                             captureBin[i].getBinSinkPad()) !=
                GST_PAD_LINK_OK) {
              g_print("ch%d capture link error!\n", i);
            } else {
              captureBin[i].stopCapture();
              g_print("ch%d capture link ok!\n", i);
            }
          } else
            g_print("ch%d capture already link!\n", i);
        }
      }

      // gst_element_sync_state_with_parent
      // gst_element_set_state(pipeline, GST_STATE_PLAYING);

      // for (i = 0; i < MAX_CHANNEL; i++) if (captureBin[i].getBinSinkPad())
      // captureBin[i].stopCapture();
    } else if (compareBuf(token, "rec", 3)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (single_enc) {
              GstPad *rec_src = encoderBin[i].getBinRecSrcPad();
              GstPad *rec_sink = muxSinkBin[i].getBinQueuePad();
              if (rec_src == NULL || rec_sink == NULL) {
                g_print("ch%d rec pad not ready!\n", i);
              } else if (gst_pad_is_linked(rec_src) != TRUE) {
                if (gst_pad_link(rec_src, rec_sink) != GST_PAD_LINK_OK) {
                  g_print("ch%d rec link error!\n", i);
                } else
                  g_print("ch%d rec link ok!\n", i);
              } else
                g_print("ch%d rec already link!\n", i);
            } else {
              if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) !=
                  TRUE) {
                if (gst_pad_link(videoBin[i / 2].getBinRecordSrcPad(i),
                                 recordBin[i].getBinSinkPad()) !=
                    GST_PAD_LINK_OK) {
                  g_print("ch%d rec link error!\n", i);
                } else
                  g_print("ch%d rec link ok!\n", i);
              } else
                g_print("ch%d rec already link!\n", i);
            }
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
          if (single_enc) {
            GstPad *rec_src = encoderBin[i].getBinRecSrcPad();
            GstPad *rec_sink = muxSinkBin[i].getBinQueuePad();
            if (rec_src == NULL || rec_sink == NULL) {
              g_print("ch%d rec pad not ready!\n", i);
            } else if (gst_pad_is_linked(rec_src) != TRUE) {
              if (gst_pad_link(rec_src, rec_sink) != GST_PAD_LINK_OK) {
                g_print("ch%d rec link error!\n", i);
              } else
                g_print("ch%d rec link ok!\n", i);
            } else
              g_print("ch%d rec already link!\n", i);
          } else {
            if (gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i)) !=
                TRUE) {
              if (gst_pad_link(videoBin[i / 2].getBinRecordSrcPad(i),
                               recordBin[i].getBinSinkPad()) !=
                  GST_PAD_LINK_OK) {
                g_print("ch%d rec link error!\n", i);
              } else
                g_print("ch%d rec link ok!\n", i);
            } else
              g_print("ch%d rec already link!\n", i);
          }
        }
      }
    } else if (compareBuf(token, "rtsp", 4)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (single_enc) {
              GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
              GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
              if (rtsp_src == NULL || rtsp_sink == NULL) {
                g_print("ch%d rtsp pad not ready!\n", i);
              } else if (gst_pad_is_linked(rtsp_src) != TRUE) {
                if (gst_pad_link(rtsp_src, rtsp_sink) != GST_PAD_LINK_OK) {
                  g_print("ch%d rtsp link error!\n", i);
                } else
                  g_print("ch%d rtsp link ok!\n", i);
              } else
                g_print("ch%d rtsp already link!\n", i);
            } else {
              if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) !=
                  TRUE) {
                if (gst_pad_link(videoBin[i / 2].getBinRtspSrcPad(i),
                                 rtspServerBin[i].getBinSinkPad()) !=
                    GST_PAD_LINK_OK) {
                  g_print("ch%d rtsp link error!\n", i);
                } else
                  g_print("ch%d rtsp link ok!\n", i);
              } else
                g_print("ch%d rtsp already link!\n", i);
            }
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
          if (single_enc) {
            GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
            GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
            if (rtsp_src == NULL || rtsp_sink == NULL) {
              g_print("ch%d rtsp pad not ready!\n", i);
            } else if (gst_pad_is_linked(rtsp_src) != TRUE) {
              if (gst_pad_link(rtsp_src, rtsp_sink) != GST_PAD_LINK_OK) {
                g_print("ch%d rtsp link error!\n", i);
              } else
                g_print("ch%d rtsp link ok!\n", i);
            } else
              g_print("ch%d rtsp already link!\n", i);
          } else {
            if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) != TRUE) {
              if (gst_pad_link(videoBin[i / 2].getBinRtspSrcPad(i),
                               rtspServerBin[i].getBinSinkPad()) !=
                  GST_PAD_LINK_OK) {
                g_print("ch%d rtsp link error!\n", i);
              } else
                g_print("ch%d rtsp link ok!\n", i);
            } else
              g_print("ch%d rtsp already link!\n", i);
          }
        }
      }
    } else
      g_print("wrong cmd!\n");
  } // link
  else if (compareBuf(token, "unlink", 6)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        // gst_element_set_state(pipeline, GST_STATE_PAUSED);
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) ==
                TRUE) {
              if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i),
                                 captureBin[i].getBinSinkPad())) {
                g_print("ch%d capture unlink ok!\n", i);
              } else {
                captureBin[i].stopCapture();
                g_print("ch%d capture unlink err!\n", i);
              }
            } else
              g_print("ch%d capture already unlink!\n", i);
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
          if (gst_pad_is_linked(videoBin[i / 2].getBinCaptureSrcPad(i)) ==
              TRUE) {
            if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i),
                               captureBin[i].getBinSinkPad())) {
              g_print("ch%d capture unlink ok!\n", i);
            } else {
              captureBin[i].stopCapture();
              g_print("ch%d capture unlink err!\n", i);
            }
          } else
            g_print("ch%d capture already unlink!\n", i);
        }
      }

      // gst_element_sync_state_with_parent
      // gst_element_set_state(pipeline, GST_STATE_PLAYING);

      // for (i = 0; i < MAX_CHANNEL; i++) if (captureBin[i].getBinSinkPad())
      // captureBin[i].stopCapture();
    } else if (compareBuf(token, "rec", 3)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (single_enc) {
              GstPad *rec_src = encoderBin[i].getBinRecSrcPad();
              GstPad *rec_sink = muxSinkBin[i].getBinQueuePad();
              if (rec_src == NULL || rec_sink == NULL) {
                g_print("ch%d rec pad not ready!\n", i);
              } else if (gst_pad_is_linked(rec_src) == TRUE) {
                if (gst_pad_unlink(rec_src, rec_sink)) {
                  g_print("ch%d rec unlink ok!\n", i);
                } else
                  g_print("ch%d rec unlink err!\n", i);
              } else
                g_print("ch%d rec already unlink!\n", i);
            } else {
              if (1) //(gst_pad_is_linked(videoBin[i / 2].getBinRecordSrcPad(i))
                     //== TRUE)
              {
                if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i),
                                   recordBin[i].getBinSinkPad())) {
                  g_print("ch%d rec unlink ok!\n", i);
                } else
                  g_print("ch%d rec unlink err!\n", i);
              } else
                g_print("ch%d rec already unlink!\n", i);
            }
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
          if (single_enc) {
            GstPad *rec_src = encoderBin[i].getBinRecSrcPad();
            GstPad *rec_sink = muxSinkBin[i].getBinQueuePad();
            if (rec_src == NULL || rec_sink == NULL) {
              g_print("ch%d rec pad not ready!\n", i);
            } else if (gst_pad_is_linked(rec_src) == TRUE) {
              if (gst_pad_unlink(rec_src, rec_sink)) {
                g_print("ch%d rec unlink ok!\n", i);
              } else
                g_print("ch%d rec unlink err!\n", i);
            } else
              g_print("ch%d rec already unlink!\n", i);
          } else {
            if (1) {
              if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i),
                                 recordBin[i].getBinSinkPad())) {
                g_print("ch%d rec unlink ok!\n", i);
              } else
                g_print("ch%d rec unlink err!\n", i);
            } else
              g_print("ch%d rec already unlink!\n", i);
          }
        }
      }
    } else if (compareBuf(token, "rtsp", 4)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (single_enc) {
              GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
              GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
              if (rtsp_src == NULL || rtsp_sink == NULL) {
                g_print("ch%d rtsp pad not ready!\n", i);
              } else if (gst_pad_is_linked(rtsp_src) == TRUE) {
                if (gst_pad_unlink(rtsp_src, rtsp_sink)) {
                  g_print("ch%d rtsp unlink ok!\n", i);
                } else
                  g_print("ch%d rtsp unlink err!\n", i);
              } else
                g_print("ch%d rtsp already unlink!\n", i);
            } else {
              if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) ==
                  TRUE) {
                if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i),
                                   rtspServerBin[i].getBinSinkPad())) {
                  g_print("ch%d rtsp unlink ok!\n", i);
                } else
                  g_print("ch%d rtsp unlink err!\n", i);
              } else
                g_print("ch%d rtsp already unlink!\n", i);
            }
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
          if (single_enc) {
            GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
            GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
            if (rtsp_src == NULL || rtsp_sink == NULL) {
              g_print("ch%d rtsp pad not ready!\n", i);
            } else if (gst_pad_is_linked(rtsp_src) == TRUE) {
              if (gst_pad_unlink(rtsp_src, rtsp_sink)) {
                g_print("ch%d rtsp unlink ok!\n", i);
              } else
                g_print("ch%d rtsp unlink err!\n", i);
            } else
              g_print("ch%d rtsp already unlink!\n", i);
          } else {
            if (gst_pad_is_linked(videoBin[i / 2].getBinRtspSrcPad(i)) == TRUE) {
              if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i),
                                 rtspServerBin[i].getBinSinkPad())) {
                g_print("ch%d rtsp unlink ok!\n", i);
              } else
                g_print("ch%d rtsp unlink err!\n", i);
            } else
              g_print("ch%d rtsp already unlink!\n", i);
          }
        }
      }
    } else
      g_print("wrong cmd!\n");
  } // unlink
  else if (compareBuf(token, "add", 3)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            if (captureBin[i].addBinToPipe(pipeline))
              g_print("ch%d capture bin add\n", i);
            else
              g_print("ch%d capture bin add error\n", i);
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
          // g_print("ch%d caputre bin add\n", i);
          if (captureBin[i].addBinToPipe(pipeline))
            g_print("ch%d capture bin add\n", i);
          else
            g_print("ch%d capture bin add error\n", i);
        }
      }
    } else if (compareBuf(token, "rec", 3)) {
      if (single_enc) {
        g_print("single-encoder mode: record bin add not supported\n");
        return -1;
      }
      token = strtok(NULL, SPLIT_CHAR);
      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (recordBin[i].addBinToPipe(pipeline))
              g_print("ch%d record bin add\n", i);
            else
              g_print("ch%d record bin add error\n", i);
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
          // g_print("ch%d caputre bin add\n", i);
          if (recordBin[i].addBinToPipe(pipeline))
            g_print("ch%d record bin add\n", i);
          else
            g_print("ch%d record bin add error\n", i);
        }
      }
    } else if (compareBuf(token, "rtsp", 4)) {
      token = strtok(NULL, SPLIT_CHAR);
      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (rtspServerBin[i].addBinToPipe(pipeline))
              g_print("ch%d rtsp bin add\n", i);
            else
              g_print("ch%d rtsp bin add error\n", i);
          }
        }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
          // g_print("ch%d caputre bin add\n", i);
          if (rtspServerBin[i].addBinToPipe(pipeline))
            g_print("ch%d rtsp bin add\n", i);
          else
            g_print("ch%d rtsp bin add error\n", i);
        }
      }
    } else
      g_print("wrong cmd!\n");
  } // add
  else if (compareBuf(token, "rm", 2)) // else if (!strncmp(buffer, "rm", 2))
  {
    token = strtok(NULL, SPLIT_CHAR);

    if (compareBuf(token, "cap", 3)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_CAP]) {
            if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i),
                               captureBin[i].getBinSinkPad()) != TRUE) {
              g_print("ch%d capture unlink error!\n", i);
            } else
              g_print("ch%d capture unlink ok!\n", i);

            if (captureBin[i].removeBinToPipe(pipeline))
              g_print("ch%d capture bin remove\n", i);
            else
              g_print("ch%d capture bin remove error\n", i);
          }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (gst_pad_unlink(videoBin[i / 2].getBinCaptureSrcPad(i),
                           captureBin[i].getBinSinkPad()) != TRUE) {
          g_print("ch%d capture unlink error!\n", i);
        } else {
          g_print("ch%d capture unlink ok!\n", i);
          if (captureBin[i].removeBinToPipe(pipeline))
            g_print("ch%d capture bin remove\n", i);
          else
            g_print("ch%d capture bin remove error\n", i);
        }
      }
    } else if (compareBuf(token, "rec", 3)) {
      if (single_enc) {
        g_print("single-encoder mode: record bin remove not supported\n");
        return -1;
      }
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC]) {
            if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i),
                               recordBin[i].getBinSinkPad()) != TRUE) {
              g_print("ch%d recordBin unlink error!\n", i);
            } else {
              g_print("ch%d recordBin unlink ok!\n", i);
              if (recordBin[i].removeBinToPipe(pipeline))
                g_print("ch%d record bin remove\n", i);
              else
                g_print("ch%d record bin remove error\n", i);
            }
          }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (gst_pad_unlink(videoBin[i / 2].getBinRecordSrcPad(i),
                           recordBin[i].getBinSinkPad()) != TRUE) {
          g_print("ch%d recordBin unlink error!\n", i);
        } else {
          g_print("ch%d recordBin unlink ok!\n", i);
          if (recordBin[i].removeBinToPipe(pipeline))
            g_print("ch%d record bin remove\n", i);
          else
            g_print("ch%d record bin remove error\n", i);
        }
      }
    } else if (compareBuf(token, "rtsp", 4)) {
      token = strtok(NULL, SPLIT_CHAR);

      if (token == NULL) {
        for (i = 0; i < MAX_CHANNEL; i++)
          if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP]) {
            if (single_enc) {
              GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
              GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
              if (rtsp_src == NULL || rtsp_sink == NULL) {
                g_print("ch%d rtspServerBin unlink error!\n", i);
              } else if (gst_pad_unlink(rtsp_src, rtsp_sink) != TRUE) {
                g_print("ch%d rtspServerBin unlink error!\n", i);
              } else {
                g_print("ch%d rtspServerBin unlink ok!\n", i);
                if (rtspServerBin[i].removeBinToPipe(pipeline))
                  g_print("ch%d rtspServerBin remove\n", i);
                else
                  g_print("ch%d record bin remove error\n", i);
              }
            } else {
              if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i),
                                 rtspServerBin[i].getBinSinkPad()) != TRUE) {
                g_print("ch%d rtspServerBin unlink error!\n", i);
              } else {
                g_print("ch%d rtspServerBin unlink ok!\n", i);
                if (rtspServerBin[i].removeBinToPipe(pipeline))
                  g_print("ch%d rtspServerBin remove\n", i);
                else
                  g_print("ch%d record bin remove error\n", i);
              }
            }
          }
      } else {
        i = charArrayToInt(token);

        if (i < 0 || i > 3) {
          g_print("channel %d not supported\n", i);
          return -1;
        }

        if (single_enc) {
          GstPad *rtsp_src = encoderBin[i].getBinRtspSrcPad();
          GstPad *rtsp_sink = rtspServerBin[i].getBinSinkPad();
          if (rtsp_src == NULL || rtsp_sink == NULL) {
            g_print("ch%d rtspServerBin unlink error!\n", i);
          } else if (gst_pad_unlink(rtsp_src, rtsp_sink) != TRUE) {
            g_print("ch%d rtspServerBin unlink error!\n", i);
          } else {
            g_print("ch%d rtspServerBin unlink ok!\n", i);
            if (rtspServerBin[i].removeBinToPipe(pipeline))
              g_print("ch%d rtspServerBin remove\n", i);
            else
              g_print("ch%d record bin remove error\n", i);
          }
        } else {
          if (gst_pad_unlink(videoBin[i / 2].getBinRtspSrcPad(i),
                             rtspServerBin[i].getBinSinkPad()) != TRUE) {
            g_print("ch%d rtspServerBin unlink error!\n", i);
          } else {
            g_print("ch%d rtspServerBin unlink ok!\n", i);
            if (rtspServerBin[i].removeBinToPipe(pipeline))
              g_print("ch%d rtspServerBin remove\n", i);
            else
              g_print("ch%d record bin remove error\n", i);
          }
        }
      }
    } else
      g_print("wrong cmd!\n");
  } // rm
  else {
    g_print("wrong cmd!\n");
  }
#endif

  return 0;
}
