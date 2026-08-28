/*
 *
 * Cantops videoBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "videoBin.h"
#include "max9296Controls.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Custom V4L2 controls for per-channel settings in dual-channel mode */
#define V4L2_CID_EXPOSURE_AUTO_CH0 (V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_EXPOSURE_AUTO_CH1 (V4L2_CID_USER_BASE + 0x1001)
#define V4L2_CID_AUTO_WHITE_BALANCE_CH0 (V4L2_CID_USER_BASE + 0x1002)
#define V4L2_CID_AUTO_WHITE_BALANCE_CH1 (V4L2_CID_USER_BASE + 0x1003)
#define V4L2_CID_AUTOGAIN_CH0 (V4L2_CID_USER_BASE + 0x1004)
#define V4L2_CID_AUTOGAIN_CH1 (V4L2_CID_USER_BASE + 0x1005)
#define V4L2_CID_GAIN_CH0 (V4L2_CID_USER_BASE + 0x1006)
#define V4L2_CID_GAIN_CH1 (V4L2_CID_USER_BASE + 0x1007)
#define V4L2_CID_HFLIP_CH0 (V4L2_CID_USER_BASE + 0x1008)
#define V4L2_CID_HFLIP_CH1 (V4L2_CID_USER_BASE + 0x1009)
#define V4L2_CID_VFLIP_CH0 (V4L2_CID_USER_BASE + 0x100A)
#define V4L2_CID_VFLIP_CH1 (V4L2_CID_USER_BASE + 0x100B)

/* Exposure time control (custom): shared across both channels per CSI */
#define V4L2_CID_EXT_TIME (V4L2_CID_USER_BASE + 0x1015)

/* LED Flash control per-channel (AR0234 R0x3270 via AP1302 DMA) */
#define V4L2_CID_LED_FLASH_CH0 (V4L2_CID_USER_BASE + 0x1018)
#define V4L2_CID_LED_FLASH_CH1 (V4L2_CID_USER_BASE + 0x1019)
/* MCP4018 digital potentiometer wiper (7-bit, 0x00~0x7F) */
#define V4L2_CID_MCP4018_WIPER_CH0 (V4L2_CID_USER_BASE + 0x101A)
#define V4L2_CID_MCP4018_WIPER_CH1 (V4L2_CID_USER_BASE + 0x101B)
/* MCP4018 VCC power via MAX9295 MFP4 GPIO (bool) */
#define V4L2_CID_MCP4018_POWER_CH0 (V4L2_CID_USER_BASE + 0x1020)
#define V4L2_CID_MCP4018_POWER_CH1 (V4L2_CID_USER_BASE + 0x1021)

typedef struct {
  guint8 csi;
  guint duration_sec;
  gboolean log_frames;
  gint64 start_us;
  guint64 warmup_count;
  guint64 frame_count;
  guint64 lost_count;
  guint64 sequence_reset_count;
  guint64 pts_backward_count;
  guint64 previous_sequence;
  GstClockTime previous_pts;
  gboolean previous_sequence_valid;
  gboolean previous_pts_valid;
} V4l2SyncTrace;

static GstPadProbeReturn v4l2_sync_trace_probe(GstPad *pad,
                                                GstPadProbeInfo *info,
                                                gpointer user_data) {
  V4l2SyncTrace *trace = (V4l2SyncTrace *)user_data;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
  gint64 now_us = g_get_monotonic_time();

  if (!trace || !buffer)
    return GST_PAD_PROBE_OK;

  guint64 sequence = GST_BUFFER_OFFSET(buffer);
  GstClockTime pts = GST_BUFFER_PTS(buffer);
  gboolean sequence_valid = GST_BUFFER_OFFSET_IS_VALID(buffer);
  gboolean pts_valid = GST_BUFFER_PTS_IS_VALID(buffer);

  /* v4l2src can emit a few zero-PTS buffers while its timestamp and sequence
   * bases are being established. A later sequence rebase in that startup
   * window is not a frame loss, so start counting at the first non-zero PTS. */
  if (trace->start_us == 0) {
    if (!pts_valid || pts == 0) {
      trace->warmup_count++;
      return GST_PAD_PROBE_OK;
    }
    trace->start_us = now_us;
  }

  if (now_us - trace->start_us >=
      (gint64)trace->duration_sec * G_USEC_PER_SEC) {
    __LOG(LOG_NOTICE,
          "[V4L2_SYNC][%s:%d] csi=%u summary duration_us=%" G_GINT64_FORMAT
          " warmup_skipped=%" G_GUINT64_FORMAT " frames=%"
          G_GUINT64_FORMAT " lost=%" G_GUINT64_FORMAT
          " sequence_resets=%" G_GUINT64_FORMAT " pts_backwards=%"
          G_GUINT64_FORMAT,
          _FILE_, __LINE__, trace->csi, now_us - trace->start_us,
          trace->warmup_count, trace->frame_count, trace->lost_count,
          trace->sequence_reset_count, trace->pts_backward_count);
    return GST_PAD_PROBE_REMOVE;
  }

  gint64 sequence_delta = -1;
  gint64 pts_delta_ns = -1;

  if (sequence_valid && trace->previous_sequence_valid) {
    if (sequence >= trace->previous_sequence) {
      sequence_delta = (gint64)(sequence - trace->previous_sequence);
      if (sequence_delta > 1)
        trace->lost_count += (guint64)(sequence_delta - 1);
    } else {
      trace->sequence_reset_count++;
    }
  }

  if (pts_valid && trace->previous_pts_valid) {
    pts_delta_ns = GST_CLOCK_DIFF(trace->previous_pts, pts);
    if (pts_delta_ns < 0)
      trace->pts_backward_count++;
  }

  trace->frame_count++;
  if (trace->log_frames) {
    __LOG(LOG_NOTICE,
          "[V4L2_SYNC][%s:%d] csi=%u sample=%" G_GUINT64_FORMAT
          " mono_ns=%" G_GINT64_FORMAT " seq_valid=%d seq=%"
          G_GUINT64_FORMAT " seq_delta=%" G_GINT64_FORMAT
          " pts_valid=%d pts_ns=%" G_GUINT64_FORMAT " pts_delta_ns=%"
          G_GINT64_FORMAT " lost_total=%" G_GUINT64_FORMAT,
          _FILE_, __LINE__, trace->csi, trace->frame_count, now_us * 1000,
          sequence_valid, sequence, sequence_delta, pts_valid, pts,
          pts_delta_ns, trace->lost_count);
  }

  trace->previous_sequence = sequence;
  trace->previous_pts = pts;
  trace->previous_sequence_valid = sequence_valid;
  trace->previous_pts_valid = pts_valid;

  return GST_PAD_PROBE_OK;
}

static void install_v4l2_sync_trace(GstElement *src, guint8 csi) {
  guint duration_sec = (guint)cmdArg.v4l2_sync_trace_sec;
  if (duration_sec == 0)
    return;

  GstPad *src_pad = gst_element_get_static_pad(src, "src");
  if (!src_pad) {
    __LOG(LOG_ERR, "[V4L2_SYNC][%s:%d] csi=%u v4l2src pad is NULL",
          _FILE_, __LINE__, csi);
    return;
  }

  V4l2SyncTrace *trace = g_new0(V4l2SyncTrace, 1);
  trace->csi = csi;
  trace->duration_sec = duration_sec;
  trace->log_frames = cmdArg.v4l2_sync_log_frames;

  gulong probe_id = gst_pad_add_probe(
      src_pad, GST_PAD_PROBE_TYPE_BUFFER, v4l2_sync_trace_probe, trace,
      (GDestroyNotify)g_free);
  gst_object_unref(src_pad);

  if (probe_id == 0) {
    g_free(trace);
    __LOG(LOG_ERR, "[V4L2_SYNC][%s:%d] csi=%u failed to install probe",
          _FILE_, __LINE__, csi);
    return;
  }

  __LOG(LOG_NOTICE,
        "[V4L2_SYNC][%s:%d] csi=%u enabled duration_sec=%u frame_log=%d "
        "source=v4l2src",
        _FILE_, __LINE__, csi, trace->duration_sec, trace->log_frames);
}

static void prepare_format(GstElement *object, gint arg0, GstCaps *caps,
                           gpointer data) {
  guint8 *csi = (guint8 *)data;
  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s [%d]", _FILE_, __LINE__, __FUNCTION__,
  //*csi);
  __LOG(LOG_INFO, "[GST][%s:%d] csi%d caps : %s", _FILE_, __LINE__, *csi,
        gst_caps_to_string(caps));
}

/**
 * Set V4L2 subdev control value via ioctl
 * @param csiNum CSI number (0 or 1)
 * @param ctrl_id V4L2 control ID (e.g., V4L2_CID_EXPOSURE_AUTO)
 * @param value Control value
 * @return 0 on success, -1 on failure
 */
static void log_v4l_subdev_name_once(int subdev_idx) {
  static int logged[16] = {0};
  if (subdev_idx < 0 || subdev_idx >= (int)(sizeof(logged) / sizeof(logged[0])))
    return;
  if (logged[subdev_idx])
    return;
  logged[subdev_idx] = 1;

  char name_path[128];
  snprintf(name_path, sizeof(name_path),
           "/sys/class/video4linux/v4l-subdev%d/name", subdev_idx);
  FILE *fp = fopen(name_path, "r");
  if (!fp)
    return;

  char buf[128];
  if (fgets(buf, sizeof(buf), fp)) {
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
      buf[len - 1] = 0;
    __LOG(LOG_INFO, "[GST][%s:%d] v4l-subdev%d name: %s", _FILE_, __LINE__,
          subdev_idx, buf);
  }
  fclose(fp);
}

/*
 * Map AWB preset name (JSON string) to AP1302 AWB_CTRL MODE nibble (0x0~0xf).
 * Unknown/NULL values default to "auto" to match DEFAULT_AWB semantics.
 * See max9296.c AP1302_AWB_MODE_* definitions.
 */
static gint awb_str_to_mode(const gchar *s) {
  if (!s)                              return 0xf;
  if (g_strcmp0(s, "auto")    == 0)    return 0xf;
  if (g_strcmp0(s, "off")     == 0)    return 0x0;
  if (g_strcmp0(s, "manual")  == 0)    return 0x0;
  if (g_strcmp0(s, "horizon") == 0)    return 0x1;
  if (g_strcmp0(s, "a")       == 0)    return 0x2;
  if (g_strcmp0(s, "cwf")     == 0)    return 0x3;
  if (g_strcmp0(s, "d50")     == 0)    return 0x4;
  if (g_strcmp0(s, "d65")     == 0)    return 0x5;
  if (g_strcmp0(s, "d75")     == 0)    return 0x6;
  if (g_strcmp0(s, "temp")    == 0)    return 0x7;
  if (g_strcmp0(s, "measure") == 0)    return 0x8;
  return 0xf;
}

static int set_v4l2_subdev_control(int csiNum, unsigned int ctrl_id,
                                   int value) {
  char dev_path[32];
  int fd, ret;
  struct v4l2_control ctrl;

  int subdev_idx =
      (csiNum == 0) ? cmdArg.v4l_subdev_csi0 : cmdArg.v4l_subdev_csi1;
  log_v4l_subdev_name_once(subdev_idx);
  snprintf(dev_path, sizeof(dev_path), "/dev/v4l-subdev%d", subdev_idx);

  fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    __LOG(LOG_ERR, "[GST][%s:%d] Failed to open %s: %s", _FILE_, __LINE__,
          dev_path, strerror(errno));
    return -1;
  }

  ctrl.id = ctrl_id;
  ctrl.value = value;

  ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
  if (ret < 0) {
    __LOG(LOG_ERR,
          "[GST][%s:%d] VIDIOC_S_CTRL failed for ctrl 0x%08x on %s: %s", _FILE_,
          __LINE__, ctrl_id, dev_path, strerror(errno));
  } else {
    __LOG(LOG_INFO, "[GST][%s:%d] Set ctrl 0x%08x = %d on %s", _FILE_, __LINE__,
          ctrl_id, value, dev_path);
  }

  close(fd);
  return ret;
}

static int apply_crop_v4l2(int csi_num, guint8 enabled_slots,
                           guint8 ch0, guint8 ch1) {
  const Max9296ZoomCenter centers[2] = {
      {cmdArg.cam[ch0].dz_x, cmdArg.cam[ch0].dz_y},
      {cmdArg.cam[ch1].dz_x, cmdArg.cam[ch1].dz_y},
  };
  const guint common_dz = cmdArg.dz[csi_num];
  Max9296CropControlBatch batch = {};
  char dev_path[32];
  int result = 0;
  int saved_errno = 0;
  guint error_idx = 0;
  const char *failed_stage = "none";

  if (max9296_crop_build_control_batch(
          cmdArg.crop_enable[csi_num] ? 1 : 0, enabled_slots, common_dz,
          centers, &batch) < 0) {
    errno = EINVAL;
    __LOG(LOG_ERR,
          "[MAX9296_CROP] csi=%d enable=%d slots=0x%x dz=%u "
          "ch%d_center=%u/%u ch%d_center=%u/%u stage=build "
          "error_idx=0 errno=%d(%s)",
          csi_num, cmdArg.crop_enable[csi_num] ? 1 : 0, enabled_slots,
          common_dz, ch0, centers[0].x, centers[0].y, ch1, centers[1].x,
          centers[1].y, errno, strerror(errno));
    return -1;
  }

  const guint effective_dz = static_cast<guint>(batch.tuple[0].value);
  const guint effective_x0 = static_cast<guint>(batch.tuple[1].value);
  const guint effective_y0 = static_cast<guint>(batch.tuple[2].value);
  const guint effective_x1 = static_cast<guint>(batch.tuple[3].value);
  const guint effective_y1 = static_cast<guint>(batch.tuple[4].value);
  const int subdev_idx =
      csi_num == 0 ? cmdArg.v4l_subdev_csi0 : cmdArg.v4l_subdev_csi1;
  log_v4l_subdev_name_once(subdev_idx);
  snprintf(dev_path, sizeof(dev_path), "/dev/v4l-subdev%d", subdev_idx);

  const int fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    __LOG(LOG_ERR,
          "[MAX9296_CROP] csi=%d enable=%d slots=0x%x dz=%u "
          "ch%d_center=%u/%u ch%d_center=%u/%u stage=open "
          "error_idx=0 errno=%d(%s)",
          csi_num, batch.enable.value, enabled_slots, effective_dz, ch0,
          effective_x0, effective_y0, ch1, effective_x1, effective_y1, errno,
          strerror(errno));
    return -1;
  }

  struct v4l2_control enable_ctrl = {};
  struct v4l2_ext_control tuple[5] = {};
  struct v4l2_ext_controls ext_ctrls = {};
  enable_ctrl.id = batch.enable.id;
  enable_ctrl.value = batch.enable.value;
  if (ioctl(fd, VIDIOC_S_CTRL, &enable_ctrl) < 0) {
    result = -1;
    saved_errno = errno;
    failed_stage = "enable";
    goto out;
  }

  for (size_t i = 0; i < batch.tuple_count; ++i) {
    tuple[i].id = batch.tuple[i].id;
    tuple[i].value = batch.tuple[i].value;
  }

  ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
  ext_ctrls.count = static_cast<__u32>(batch.tuple_count);
  ext_ctrls.controls = tuple;
  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls) < 0) {
    result = -1;
    saved_errno = errno;
    error_idx = ext_ctrls.error_idx;
    failed_stage = "tuple";
  }

out:
  if (close(fd) < 0 && result == 0) {
    result = -1;
    saved_errno = errno;
    failed_stage = "close";
  }

  __LOG(result < 0 ? LOG_ERR : LOG_NOTICE,
        "[MAX9296_CROP] csi=%d enable=%d slots=0x%x dz=%u "
        "ch%d_center=%u/%u ch%d_center=%u/%u stage=%s "
        "error_idx=%u errno=%d(%s)",
        csi_num, batch.enable.value, enabled_slots, effective_dz, ch0,
        effective_x0, effective_y0, ch1, effective_x1, effective_y1,
        failed_stage, error_idx, saved_errno,
        saved_errno ? strerror(saved_errno) : "none");
  if (saved_errno)
    errno = saved_errno;
  return result;
}

/* Apply LED flash for one channel slot.
 *
 * Driver owns the MCP4018 I2C-bus gate (MAX9295 MFP4 GPIO) — it opens,
 * writes wiper, closes atomically within the wiper s_ctrl handler.
 * Userspace therefore needs only two ioctls per channel.
 *
 * Caller selects the correct wiper_id:
 *   - firmware-routed controls (led_flash) always use the CH0 slot CID
 *     in single mode (driver single-path); per-slot CIDs in dual mode.
 *   - MCP4018 is hardware-direct, so in single mode pick the CID
 *     matching the active local port (Port A = CH0, Port B = CH1).
 */
static void apply_led_flash_v4l2(int csiNum, guint8 ch_idx,
                                 unsigned int wiper_id,
                                 unsigned int flash_id) {
  gboolean en    = cmdArg.cam[ch_idx].led_flash_enable;
  guint    wiper = cmdArg.cam[ch_idx].led_flash_wiper & 0x7f;
  guint    delay = cmdArg.cam[ch_idx].led_flash_delay & 0xff;
  int flash_val  = en ? (int)((1u << 8) | delay) : 0;

  /* When the flash is disabled the LED/MCP4018 chain may be unpopulated on
   * this board. Avoid touching MCP4018 at all (prevents ENXIO and keeps
   * the driver's wiper cache clean so its replay also skips the port). */
  if (en) {
    set_v4l2_subdev_control(csiNum, wiper_id, (int)wiper);
  }
  set_v4l2_subdev_control(csiNum, flash_id, flash_val);

  __LOG(LOG_NOTICE,
        "[GST][%s:%d] ch%d led_flash: enable=%d wiper=%u delay=%u flash_reg=0x%04x%s",
        _FILE_, __LINE__, ch_idx, en ? 1 : 0, wiper, delay,
        (unsigned int)flash_val, en ? "" : " (wiper skipped)");
}

int set_v4l2_subdev_fps(int csiNum, int fps) {
  char dev_path[32];
  int fd, ret;
  struct v4l2_subdev_frame_interval fi;

  int subdev_idx =
      (csiNum == 0) ? cmdArg.v4l_subdev_csi0 : cmdArg.v4l_subdev_csi1;
  snprintf(dev_path, sizeof(dev_path), "/dev/v4l-subdev%d", subdev_idx);

  fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    __LOG(LOG_ERR, "[GST][%s:%d] Failed to open %s: %s", _FILE_, __LINE__,
          dev_path, strerror(errno));
    return -1;
  }

  memset(&fi, 0, sizeof(fi));
  fi.pad = 0;
  fi.interval.numerator = 1;
  fi.interval.denominator = fps;

  ret = ioctl(fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &fi);
  if (ret < 0) {
    __LOG(LOG_ERR, "[GST][%s:%d] S_FRAME_INTERVAL failed on %s: %s", _FILE_,
          __LINE__, dev_path, strerror(errno));
  } else {
    __LOG(LOG_NOTICE, "[GST][%s:%d] Set subdev fps=%d on %s", _FILE_,
          __LINE__, fps, dev_path);
  }

  close(fd);
  return ret;
}

void VideoBin::getIoMode() {
  gint ioMode;

  g_object_get(be.src, "io-mode", &ioMode, NULL);
  //__LOG(LOG_NOTICE, "[GST][%s:%d] csi%d get io-mode : %d", _FILE_, __LINE__,
  // csi, ioMode);
  g_print("csi%d get io-mode : %d\n", videoData.csi, ioMode);
}

void VideoBin::setFps(guint16 fps) {
  if (be.capsfilter == NULL || be.bin == NULL) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d capsfilter not initialized", _FILE_, __LINE__, videoData.csi);
    return;
  }

  GstCaps *old_caps = NULL;
  g_object_get(be.capsfilter, "caps", &old_caps, NULL);
  if (!old_caps) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d failed to get current caps", _FILE_, __LINE__, videoData.csi);
    return;
  }

  GstStructure *s = gst_caps_get_structure(old_caps, 0);
  gint width = 0, height = 0;
  gst_structure_get_int(s, "width", &width);
  gst_structure_get_int(s, "height", &height);
  gst_caps_unref(old_caps);

  if (width <= 0 || height <= 0) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d invalid caps (%dx%d), fps unchanged",
          _FILE_, __LINE__, videoData.csi, width, height);
    return;
  }

  GstCaps *new_caps = gst_caps_new_simple("video/x-raw",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, (gint)fps, 1, NULL);

  /* v4l2src 는 스트리밍 중 caps 재협상을 지원하지 않는다. PLAYING 상태에서
   * capsfilter 를 바꾸면 현재 고정된 caps 와 교집합이 없어 협상이 실패하고
   * not-negotiated(-4) 로 파이프라인이 정지한다. 소스 bin 을 READY 로 내려
   * 협상을 무효화한 뒤 caps 를 바꾸고 부모 상태로 복귀시킨다. */
  if (gst_element_set_state(be.bin, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d set READY failed, fps unchanged", _FILE_,
          __LINE__, videoData.csi);
    gst_caps_unref(new_caps);
    return;
  }
  /* 스트리밍 스레드가 완전히 멈춘 뒤에 caps 를 교체해야 한다. 전환이
   * 끝나지 않았는데 바꾸면 협상 실패로 채널이 정지하므로, 미완료면
   * caps 를 건드리지 않고 원래 상태로 되돌린다. */
  GstState cur_state = GST_STATE_VOID_PENDING;
  if (gst_element_get_state(be.bin, &cur_state, NULL, 5 * GST_SECOND) !=
          GST_STATE_CHANGE_SUCCESS ||
      cur_state != GST_STATE_READY) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d READY not reached (state:%s), fps unchanged",
          _FILE_, __LINE__, videoData.csi, gst_element_state_get_name(cur_state));
    gst_caps_unref(new_caps);
    gst_element_sync_state_with_parent(be.bin);
    return;
  }

  g_object_set(be.capsfilter, "caps", new_caps, NULL);
  gst_caps_unref(new_caps);

  if (!gst_element_sync_state_with_parent(be.bin)) {
    __LOG(LOG_ERR, "[GST][%s:%d] csi%d resume to parent state failed", _FILE_,
          __LINE__, videoData.csi);
    return;
  }

  __LOG(LOG_NOTICE, "[GST][%s:%d] csi%d set capsfilter fps=%d (%dx%d) via READY cycle",
        _FILE_, __LINE__, videoData.csi, fps, width, height);
}

void VideoBin::setIoMode(guint16 data) {
  gint ioMode;

  g_object_set(be.src, "io-mode", data, NULL);
  g_object_get(be.src, "io-mode", &ioMode, NULL);
  __LOG(LOG_INFO, "[GST][%s:%d] csi%d set io-mode : %d", _FILE_, __LINE__,
        videoData.csi, ioMode);
  g_print("csi%d set io-mode : %d\n", videoData.csi, ioMode);
}

VideoBin *VideoBin::getInstance() {
  static VideoBin instance;
  return &instance;
}

GstPad *VideoBin::getBinRtspSrcPad(guint8 ch) {
  if (srcRtspPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          ch);

  return srcRtspPad;
}

GstPad *VideoBin::getBinRecordSrcPad(guint8 ch) {
  if (srcRecordPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          ch);

  return srcRecordPad;
}

GstPad *VideoBin::getBinCaptureSrcPad(guint8 ch) {
  if (srcCapturePad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          ch);

  return srcCapturePad;
}

gboolean VideoBin::addBinRtspSrcPad(guint8 ch) {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
  GstPad *target_pad = gst_element_get_request_pad(be.teeCrop, "src_%u");
  srcRtspPad =
      gst_ghost_pad_new(g_strdup_printf("srcpad_ch%d", ch),
                        target_pad);
  if (target_pad)
      gst_object_unref(target_pad);

  return gst_element_add_pad(be.bin, srcRtspPad);
}

gboolean VideoBin::addBinRecordSrcPad(guint8 ch) {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
  GstPad *target_pad = gst_element_get_request_pad(be.teeCrop, "src_%u");
  srcRecordPad =
      gst_ghost_pad_new(g_strdup_printf("record_pad_ch%d", ch),
                        target_pad);
  if (target_pad)
      gst_object_unref(target_pad);

  if (cmdArg.levelMode == MODE_TEST) {
    gst_pad_add_probe(srcRecordPad, GST_PAD_PROBE_TYPE_BUFFER,
                      (GstPadProbeCallback)probe_function, be.bin, NULL);
  }

  return gst_element_add_pad(be.bin, srcRecordPad);
}

gboolean VideoBin::addBinCaptureSrcPad(guint8 ch) {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
  GstPad *target_pad = gst_element_get_request_pad(be.teeCrop, "src_%u");
  srcCapturePad =
      gst_ghost_pad_new(g_strdup_printf("capture_pad_ch%d", ch),
                        target_pad);
  if (target_pad)
      gst_object_unref(target_pad);

  return gst_element_add_pad(be.bin, srcCapturePad);
}

VideoBin::VideoBin() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  be.bin = NULL;
  srcRecordPad = NULL;
  srcRtspPad = NULL;
}

VideoBin::~VideoBin() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        videoData.csi);
}

gboolean VideoBin::addCrop(CropDir dir) {
  gint ret = 0;
  // g_print("csi:%d, dir:%d\n", csi, dir);
  __LOG(LOG_INFO, "[GST][%s:%d] video crop csi : %d, dir : %d", _FILE_,
        __LINE__, videoData.csi, dir);

  be.crop[dir] =
      gst_element_factory_make("videocrop", g_strdup_printf("crop%d", dir));
  be.tee[dir] = gst_element_factory_make("tee", g_strdup_printf("tee%d", dir));
  be.overlay[dir] = gst_element_factory_make("timeoverlay",
                                             g_strdup_printf("overaly%d", dir));
  be.queue[dir] =
      gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", dir));
  be.convert2[dir] = gst_element_factory_make(
      "imxvideoconvert_g2d", g_strdup_printf("convert%d", dir));

  if (dir % 2 == 0)
    g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right",
                 cmdArg.width, NULL);
  // g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right",
  // cmdArg.res[cmdArg.resMode].width, NULL);
  else
    g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.width,
                 "right", 0, NULL);
  // g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left",
  // cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);

  if (!be.crop[dir] || !be.tee[dir] || !be.overlay[dir] || !be.queue[dir] ||
      !be.convert2[dir]) {
    __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_,
          __LINE__, dir);
    return ret;
  }

  g_object_set(be.overlay[dir], "valignment", 2, NULL);
  g_object_set(be.overlay[dir], "halignment", 0, NULL);
  g_object_set(be.overlay[dir], "font-desc", "Times New Roman Italic, 16",
               NULL);
  g_object_set(be.overlay[dir], "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
  g_object_set(be.overlay[dir], "show-times-as-dates", TRUE, NULL);
  {
    GDateTime *epoch_datetime = g_date_time_new_now_local();
    g_object_set(be.overlay[dir], "datetime-epoch", epoch_datetime, NULL);
    g_date_time_unref(epoch_datetime);
  }
  g_object_set(be.queue[dir], "max-size-time", 300 * GST_MSECOND,
               "max-size-buffers", cmdArg.main_fps[videoData.csi], "leaky", 1,
               NULL);

  GstCaps *caps = gst_caps_new_simple(
      "video/x-raw", "width", G_TYPE_INT, cmdArg.width, "height", G_TYPE_INT,
      cmdArg.height, "framerate", GST_TYPE_FRACTION,
      cmdArg.main_fps[videoData.csi], 1, NULL);

  g_object_set(be.capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  gst_bin_add_many(GST_BIN(be.bin), be.crop[dir], be.tee[dir], be.overlay[dir],
                   be.queue[dir], be.convert2[dir], NULL);

  if (cmdArg.overlay_en)
    ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.overlay[dir],
                                be.convert2[dir], be.tee[dir], NULL);
  else
    ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.convert2[dir],
                                be.tee[dir], NULL);

  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] crop [%d] link err", _FILE_, __LINE__, dir);
    return ret;
  }

#if 0
    guint8 ch = csi * 2 + dir;
    g_print("ch:%d\n", ch);
    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error0");
    else
        g_message("videoBin_record_src_ch%d", ch);

    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error1");
    else
        g_message("videoBin_rtsp_src_ch%d", ch);
#endif

  return ret;
}

gboolean VideoBin::init(guint8 csiNum) {
  GstCaps *caps;
  gboolean ret = 0;
  gboolean crop_en = cmdArg.crop_en[csiNum];
  videoData.csi = csiNum;
  gint wdt_timeout;

  if (be.bin != NULL) {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s(%d) already init", _FILE_, __LINE__,
    //__FUNCTION__, csi);
    return 1;
  }

  // csiNum 0 -> channels 0,1 / csiNum 1 -> channels 2,3
  const guint8 ch_base = csiNum * 2;
  const guint8 ch0 = ch_base;
  const guint8 ch1 = ch_base + 1;
  const gboolean ch0_enabled = cmdArg.cam[ch0].enable;
  const gboolean ch1_enabled = cmdArg.cam[ch1].enable;
  const gboolean dual_mode = ch0_enabled && ch1_enabled;
  const guint8 enabled_slots = (ch0_enabled ? 0x01 : 0) |
                               (ch1_enabled ? 0x02 : 0);
  const guint8 auto_ae_slots =
      (ch0_enabled && cmdArg.cam[ch0].ae_on ? 0x01 : 0) |
      (ch1_enabled && cmdArg.cam[ch1].ae_on ? 0x02 : 0);
  const Max9296ExposurePlan exposure_plan = max9296_exposure_plan(
      cmdArg.main_fps[csiNum], enabled_slots, auto_ae_slots);

  if (exposure_plan == MAX9296_REJECT_MANUAL_EXPOSURE) {
    __LOG(LOG_CRIT,
          "[MAX9296_EXPOSURE] reject csi=%u channels=%u/%u "
          "mode=%ux%u fps=%u requested_exp=%u/%u safe_fps_max=30 "
          "reason=manual-exposure-above-safe-range",
          csiNum, ch0, ch1, cmdArg.width, cmdArg.height,
          cmdArg.main_fps[csiNum], cmdArg.cam[ch0].exp_time,
          cmdArg.cam[ch1].exp_time);
    return FALSE;
  }

  if (apply_crop_v4l2(csiNum, enabled_slots, ch0, ch1) < 0) {
    __LOG(LOG_CRIT,
          "[MAX9296_CROP] csi=%u initialization failed before prepare",
          csiNum);
    return FALSE;
  }

  if ((cmdArg.cam[0].enable || cmdArg.cam[1].enable) &&
      (cmdArg.cam[2].enable || cmdArg.cam[3].enable))
    wdt_timeout = cmdArg.wdt_timeout_long;
  else
    wdt_timeout = cmdArg.wdt_timeout_short;

  // if(!cmdArg.stream_en[STREAM_REC])
  //     wdt_timeout = 0;

  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d] crop : %s, wdt_timeout : %d", _FILE_,
        __LINE__, __FUNCTION__, csiNum, crop_en ? "enable" : "disable",
        wdt_timeout);

  be.bin = gst_bin_new(g_strdup_printf("videoBin%d", csiNum));
  be.src = gst_element_factory_make("v4l2src", "src");
  be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
  be.capsfilter = gst_element_factory_make("capsfilter", "caps");
  be.teeCrop = gst_element_factory_make("tee", "teeCrop");
  be.queue_main = gst_element_factory_make(QUEUE_TYPE, "queue_main");
  be.deinterlace = gst_element_factory_make("deinterlace", "deinterlace");
  if (cmdArg.videorate_en)
    be.rate = gst_element_factory_make("videorate", "videorate");
  else
    be.rate = NULL;
  be.watchdog = gst_element_factory_make("watchdog", "watchdog");

  if (!be.bin || !be.src || !be.capsfilter || !be.teeCrop || !be.convert ||
      !be.queue_main || !be.deinterlace || !be.watchdog ||
      (cmdArg.videorate_en && !be.rate)) {
    __LOG(LOG_CRIT, "[GST][%s:%d] video main element create error", _FILE_,
          __LINE__);
    return ret;
  }
  if (cmdArg.videorate_en)
    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.capsfilter,
                     be.teeCrop, be.queue_main, be.deinterlace, be.rate,
                     be.watchdog, NULL);
  else
    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.capsfilter,
                     be.teeCrop, be.queue_main, be.deinterlace,
                     be.watchdog, NULL);

  g_object_set(
      be.src, "io-mode", cmdArg.ioMode,
      NULL); // 0:auto, 1:rw, 2:mmap, 3:userptr, 4:dmabuf, 5:dmabuf-import
  g_object_set(be.src, "do-timestamp", TRUE, NULL);
  // g_object_set(be.src, "num-buffers", 10, NULL);
  // g_object_set(be.deinterlace, "mode", 1, NULL);
  // g_object_set(be.src, "pixel-aspect-ratio", "1/1", NULL);
  g_signal_connect(be.src, "prepare-format", G_CALLBACK(prepare_format),
                   &csiNum);
  // [Queue 최적화] 시간 기반 고정 버퍼링 (JSON 설정값 사용)
  // max-size-buffers=0 (무제한)으로 설정하여 시간 기준으로만 제어
  g_object_set(be.queue_main, "max-size-time", cmdArg.queue_main_src_time_ms * GST_MSECOND,
               "max-size-buffers", 0, "leaky",
               LEAKY_DOWNSTREAM, NULL);
  g_object_set(be.watchdog, "timeout", wdt_timeout, NULL);

#if 1
  // Exposure time is shared (global) across both channels
  if (exposure_plan == MAX9296_WRITE_EXPOSURE_SEED) {
    // Use ext_time from i2c config (shared between channels)
    guint32 ext_time =
        ch0_enabled ? cmdArg.cam[ch0].exp_time : cmdArg.cam[ch1].exp_time;
    set_v4l2_subdev_control(csiNum, V4L2_CID_EXT_TIME, ext_time);
  } else {
    __LOG(LOG_NOTICE,
          "[MAX9296_EXPOSURE] skip seed csi=%u channels=%u/%u "
          "mode=%ux%u fps=%u policy=auto-ae-high-fps",
          csiNum, ch0, ch1, cmdArg.width, cmdArg.height,
          cmdArg.main_fps[csiNum]);
  }

  if (dual_mode) {
    __LOG(LOG_NOTICE,
          "[GST][%s:%d] Dual-channel mode detected for csi%d (ch%d + ch%d)",
          _FILE_, __LINE__, csiNum, ch0, ch1);

    // Channel 0 settings
    int ae_on_ch0 = cmdArg.cam[ch0].ae_on ? 1 : 0; // 1=auto, 0=manual
    set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE_AUTO_CH0, ae_on_ch0);
    set_v4l2_subdev_control(csiNum, V4L2_CID_AUTOGAIN_CH0,
                            cmdArg.cam[ch0].ae_on ? 1 : 0);
    set_v4l2_subdev_control(csiNum, V4L2_CID_GAIN_CH0, cmdArg.cam[ch0].ae_gain);

    gint awb_ch0 = awb_str_to_mode(cmdArg.cam[ch0].awb);
    set_v4l2_subdev_control(csiNum, V4L2_CID_AUTO_WHITE_BALANCE_CH0, awb_ch0);

    set_v4l2_subdev_control(csiNum, V4L2_CID_HFLIP_CH0,
                            cmdArg.cam[ch0].hflip ? 1 : 0);
    set_v4l2_subdev_control(csiNum, V4L2_CID_VFLIP_CH0,
                            cmdArg.cam[ch0].vflip ? 1 : 0);

    __LOG(
        LOG_NOTICE,
        "[GST][%s:%d] ch%d controls (dual, csi%d CH0 slot): ae_on=%d gain=%d "
        "exp_time=%u awb=%s(0x%x) hflip=%d vflip=%d",
        _FILE_, __LINE__, ch0, csiNum, ae_on_ch0, cmdArg.cam[ch0].ae_gain,
        cmdArg.cam[ch0].exp_time, cmdArg.cam[ch0].awb, awb_ch0,
        cmdArg.cam[ch0].hflip, cmdArg.cam[ch0].vflip);

    apply_led_flash_v4l2(csiNum, ch0,
                         V4L2_CID_MCP4018_WIPER_CH0,
                         V4L2_CID_LED_FLASH_CH0);

    // Channel 1 settings
    int ae_on_ch1 = cmdArg.cam[ch1].ae_on ? 1 : 0; // 1=auto, 0=manual
    set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE_AUTO_CH1, ae_on_ch1);
    set_v4l2_subdev_control(csiNum, V4L2_CID_AUTOGAIN_CH1,
                            cmdArg.cam[ch1].ae_on ? 1 : 0);
    set_v4l2_subdev_control(csiNum, V4L2_CID_GAIN_CH1, cmdArg.cam[ch1].ae_gain);

    gint awb_ch1 = awb_str_to_mode(cmdArg.cam[ch1].awb);
    set_v4l2_subdev_control(csiNum, V4L2_CID_AUTO_WHITE_BALANCE_CH1, awb_ch1);

    set_v4l2_subdev_control(csiNum, V4L2_CID_HFLIP_CH1,
                            cmdArg.cam[ch1].hflip ? 1 : 0);
    set_v4l2_subdev_control(csiNum, V4L2_CID_VFLIP_CH1,
                            cmdArg.cam[ch1].vflip ? 1 : 0);

    __LOG(
        LOG_NOTICE,
        "[GST][%s:%d] ch%d controls (dual, csi%d CH1 slot): ae_on=%d gain=%d "
        "exp_time=%u awb=%s(0x%x) hflip=%d vflip=%d",
        _FILE_, __LINE__, ch1, csiNum, ae_on_ch1, cmdArg.cam[ch1].ae_gain,
        cmdArg.cam[ch1].exp_time, cmdArg.cam[ch1].awb, awb_ch1,
        cmdArg.cam[ch1].hflip, cmdArg.cam[ch1].vflip);

    apply_led_flash_v4l2(csiNum, ch1,
                         V4L2_CID_MCP4018_WIPER_CH1,
                         V4L2_CID_LED_FLASH_CH1);
  } else {
    // Single-channel mode: use per-channel custom controls on the active
    // channel
    CamConfig *cam_cfg = ch0_enabled ? &cmdArg.cam[ch0] : &cmdArg.cam[ch1];
    guint8 active_ch = ch0_enabled ? ch0 : ch1;
    int ae_on = cam_cfg->ae_on ? 1 : 0; // 1=auto, 0=manual
    // Single-channel mode: max9296 driver's apply_cached_controls() reads
    // only ctrl_cache.ch0 regardless of the physical channel, so target the
    // CH0 slot. The i2c write still goes to the subdev's single address.
    unsigned int ae_ctrl_id  = V4L2_CID_EXPOSURE_AUTO_CH0;
    unsigned int autogain_id = V4L2_CID_AUTOGAIN_CH0;
    unsigned int gain_id     = V4L2_CID_GAIN_CH0;
    unsigned int awb_id      = V4L2_CID_AUTO_WHITE_BALANCE_CH0;
    unsigned int hflip_id    = V4L2_CID_HFLIP_CH0;
    unsigned int vflip_id    = V4L2_CID_VFLIP_CH0;
    __LOG(LOG_NOTICE, "[GST][%s:%d] Single-channel mode for csi%d (ch%d)",
          _FILE_, __LINE__, csiNum, active_ch);

    set_v4l2_subdev_control(csiNum, ae_ctrl_id, ae_on);
    set_v4l2_subdev_control(csiNum, autogain_id, cam_cfg->ae_on ? 1 : 0);
    set_v4l2_subdev_control(csiNum, gain_id, cam_cfg->ae_gain);

    gint awb_auto = awb_str_to_mode(cam_cfg->awb);
    set_v4l2_subdev_control(csiNum, awb_id, awb_auto);

    set_v4l2_subdev_control(csiNum, hflip_id, cam_cfg->hflip ? 1 : 0);
    set_v4l2_subdev_control(csiNum, vflip_id, cam_cfg->vflip ? 1 : 0);

    __LOG(LOG_NOTICE,
          "[GST][%s:%d] ch%d controls (single, csi%d CH0 slot): ae_on=%d "
          "gain=%d exp_time=%u awb=%s(0x%x) hflip=%d vflip=%d",
          _FILE_, __LINE__, active_ch, csiNum, ae_on, cam_cfg->ae_gain,
          cam_cfg->exp_time, cam_cfg->awb, awb_auto, cam_cfg->hflip,
          cam_cfg->vflip);

    /* single mode:
     *   - led_flash: CH0 slot CID (firmware routes via AP1302 global addr).
     *   - MCP4018: hardware-direct, pick CID matching the active local port
     *     (Port A = local CH0, Port B = local CH1). ch0_enabled distinguishes. */
    unsigned int mcp_wiper_id = ch0_enabled ? V4L2_CID_MCP4018_WIPER_CH0
                                            : V4L2_CID_MCP4018_WIPER_CH1;
    apply_led_flash_v4l2(csiNum, active_ch,
                         mcp_wiper_id,
                         V4L2_CID_LED_FLASH_CH0);
  }

#endif
  if (cmdArg.levelMode == MODE_TEST) {
    // GstPad *srcpad = gst_element_get_static_pad(be.src, "src");
    // gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER,
    // (GstPadProbeCallback)probe_function, be.src, NULL);
    // gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER,
    // (GstPadProbeCallback)probe_function, be.capsfilter, NULL);
    // gst_object_unref(srcpad);
  }

  if (csiNum == 0) {
    char video_dev[32];
    snprintf(video_dev, sizeof(video_dev), "/dev/video%d",
             cmdArg.v4l_video_csi0);
    __LOG(LOG_INFO, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__,
          video_dev);
    g_object_set(be.src, "device", video_dev, NULL);
  } else if (csiNum == 1) {
    char video_dev[32];
    snprintf(video_dev, sizeof(video_dev), "/dev/video%d",
             cmdArg.v4l_video_csi1);
    __LOG(LOG_INFO, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__,
          video_dev);
    g_object_set(be.src, "device", video_dev, NULL);
  }

  install_v4l2_sync_trace(be.src, csiNum);

  if (crop_en) {
    caps = gst_caps_new_simple("video/x-raw",
                               //"format", G_TYPE_STRING, "RGBx",
                               //"format", G_TYPE_STRING, "NV12",
                               "width", G_TYPE_INT, cmdArg.width * 2, "height",
                               G_TYPE_INT, cmdArg.height, "framerate",
                               GST_TYPE_FRACTION, cmdArg.main_fps[csiNum], 1,
                               //"colorimetry", G_TYPE_STRING, "1:4:5:1",
                               //"interlace-mode", G_TYPE_STRING, "progressive",
                               //"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                               NULL);

    ret = gst_element_link_many(be.src, be.watchdog, be.convert, be.capsfilter,
                                be.teeCrop, NULL);
    // ret = gst_element_link_filtered(be.src, be.teeCrop, caps);
    if (!ret) {
      __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
      return ret;
    }
  } else {
    caps = gst_caps_new_simple("video/x-raw",
                               //"format", G_TYPE_STRING, "NV12",
                               "width", G_TYPE_INT, cmdArg.width, "height",
                               G_TYPE_INT, cmdArg.height, "framerate",
                               GST_TYPE_FRACTION, cmdArg.main_fps[csiNum], 1,
                               //"colorimetry", G_TYPE_STRING, "1:4:5:1",
                               //"interlace-mode", G_TYPE_STRING, "progressive",
                               //"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                               NULL);

    ret = gst_element_link_many(be.src, be.watchdog, be.capsfilter, be.teeCrop,
                                NULL);
    // ret = gst_element_link_filtered(be.src, be.teeCrop, caps);
    if (!ret) {
      __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
      return ret;
    }
  }

  g_object_set(be.capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  ret = gst_bin_add(GST_BIN(pipeline), be.bin);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] video bin add error in pipeline", _FILE_,
          __LINE__);
    return ret;
  }

  return ret;
}
