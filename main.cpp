/*
 *
 * Cantops main.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "aes.h"
#include "audioBin.h"
#include "captureBin.h"
#include "encoderBin.h"
#include "healthProducer.h"
#include "ipc.h"
#include "max9296Prepare.h"
#include "muxSinkBin.h"
#include "parser.h"
#include "recordBin.h"
#include "rtspServerBin.h"
#include "tcpServer.h"
#include "testBin.h"
#include "util.h"
#include "videoBin.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <execinfo.h> // For backtrace

#define APP_VERSION "3.0"
#define MAX_SNAPBACK_DRIFT_MS 30000
#define MIN_SPLIT_INTERVAL_SEC 5
#define SNAP_BACK_GRACE_PERIOD_MS 58000
#define MILLISECONDS_IN_MINUTE 60000
/* 분할 직후 ~ 새 조각 open(format_location) 전까지의 '값 없음' 표식.
 * split_msec 은 분 내 오프셋이라 항상 0..59999 이므로 음수는 충돌하지 않는다. */
#define SPLIT_MSEC_UNSET (-1)
#define CAM_STATE_RECORDING_DIR "/tmp/cam_state/recording"
#define CAM_STATE_RECORDING_ACTUAL_FILE CAM_STATE_RECORDING_DIR "/start_video_time_actual"

#define SEGFAULT_DEBUG
#define RECORDBIN_ENABLE
#define RTSPSERVERBIN_ENABLE
#define AUDIOBIN_ENABLE
#define SPLIT_TIME_RECOVERY

typedef struct _FragmentClosedEvent {
  gint ch;
  gchar *location;
  GstClockTime running_time;
  GstClockTime duration;
  gboolean in_use; // 풀 관리용 플래그
} FragmentClosedEvent;

#define MAX_EVENT_POOL_SIZE 16
static FragmentClosedEvent event_pool[MAX_EVENT_POOL_SIZE];
static GMutex pool_mutex;

// 드라이버 sysfs에서 읽은 disconnect 비트마스크 (bit0=ch0, bit1=ch1, bit2=ch2, bit3=ch3)
int g_link_disconnect_mask = 0;

/* 파이프라인이 ASYNC_DONE(preroll 완료)에 도달했는지. sink 중 하나라도 버퍼를
 * 받지 못하면 여기 도달하지 못하고 파일이 하나도 생기지 않는다. */
static gboolean g_async_done_seen = FALSE;

#define LINK_STATUS_RECHECK_SEC 5   /* PLAYING 후 link_status 재확인 시점 */
#define PLAYING_WATCH_SEC       15  /* PLAYING 요청 후 preroll 완료 감시 시점 */

/* link_status 는 3상태다: 0=connected, >0=disconnect, <0/읽기실패=unverified.
 * unverified 를 disconnect 로 뭉개면 멀쩡한 채널을 정렬 판정에서 배제해 버리고,
 * connected 로 뭉개면 끊긴 채널을 계속 물고 간다. 그래서 미확인은 이전 판정을 유지한다.
 * 드라이버는 STREAMON(= PLAYING 전이) 때 max9296_load_regs() 에서 이 값을 채우므로
 * PLAYING 이전 읽기는 -1(미확인)이 정상이며, 그래서 PLAYING 이후 재확인이 필요하다. */
static void update_link_disconnect_mask(VideoBin *vb, const gchar *phase) {
  static const char *sysfs_link[] = {
      "/sys/bus/i2c/devices/2-0048/link_status",  // CSI0 (i2c2, ch0/ch1)
      "/sys/bus/i2c/devices/1-0048/link_status",  // CSI1 (i2c1, ch2/ch3)
  };

  for (int idx = 0; idx < MAX_VIDEO_SRC; idx++) {
    if (vb[idx].be.bin == NULL) continue;

    int link_val = -1;
    const gchar *how = "read fail";
    int fd = open(sysfs_link[idx], O_RDONLY);
    if (fd < 0) {
      how = "open fail";
    } else {
      char buf[16] = {0};
      int n = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if (n > 0) {
        link_val = atoi(buf);
        how = "sysfs";
      }
    }

    int mask_bits = (0x3 << (idx * 2));
    const gchar *state;
    if (link_val < 0) {
      state = "unverified";  /* 이전 판정 유지 - disconnect 로 취급하지 않는다 */
    } else if (link_val > 0) {
      g_link_disconnect_mask |= mask_bits;
      state = "disconnect";
    } else {
      g_link_disconnect_mask &= ~mask_bits;
      state = "connected";
    }

    __LOG((link_val > 0) ? LOG_WARNING : LOG_NOTICE,
          "[GST][%s:%d] CSI%d(ch%d/ch%d) link_status=%d (%s via %s) phase=%s mask=0x%x",
          _FILE_, __LINE__, idx, idx * 2, idx * 2 + 1, link_val, state, how,
          phase, g_link_disconnect_mask);
  }
}

static gboolean link_status_recheck_cb(gpointer data) {
  update_link_disconnect_mask((VideoBin *)data, "post-playing");
  return G_SOURCE_REMOVE;
}

/* PLAYING 을 요청했는데도 preroll 이 끝나지 않으면 파일이 하나도 안 생긴다.
 * 지금까지 이 실패는 'Got async-done message' 로그의 '부재' 로만 드러나서
 * 정상 로그와 대조해야만 알 수 있었다. */
static gboolean playing_watch_cb(gpointer data) {
  if (!g_async_done_seen) {
    __LOG(LOG_ERR,
          "[GST][%s:%d] pipeline NOT prerolled %ds after PLAYING request (no ASYNC_DONE)"
          " - some sink never received a buffer, so no files will be created."
          " Check the per-channel 'NO DATA' warnings to find which source is dead.",
          _FILE_, __LINE__, PLAYING_WATCH_SEC);
  }
  return G_SOURCE_REMOVE;
}

static GAsyncQueue *fragment_closed_queue = NULL;
static GThread *fragment_closed_thread = NULL;

static gpointer fragment_closed_worker(gpointer data) {
  MuxSinkBin *msBin = (MuxSinkBin *)data;

  while (TRUE) {
    FragmentClosedEvent *ev =
        (FragmentClosedEvent *)g_async_queue_pop(fragment_closed_queue);
    if (!ev)
      continue;

    if (ev->ch < 0) {
      // 종료 시그널 (이벤트 풀에 속하지 않은 더미 객체인 경우 처리)
      if (!ev->in_use) g_free(ev); 
      break;
    }

    msBin[ev->ch].handleFragmentClosed(ev->location, ev->running_time, ev->duration);
    healthProducerNoteFragmentClosed(ev->ch, ev->location);

    // 객체 반환 (풀로 복구)
    if (ev->location) {
      g_free(ev->location);
      ev->location = NULL;
    }
    g_mutex_lock(&pool_mutex);
    ev->in_use = FALSE;
    g_mutex_unlock(&pool_mutex);
  }
  return NULL;
}

static void start_fragment_closed_worker(MuxSinkBin *msBin) {
  if (fragment_closed_queue)
    return;

  fragment_closed_queue = g_async_queue_new();
  fragment_closed_thread =
      g_thread_new("fragment-closed-worker", fragment_closed_worker, msBin);
}

static void stop_fragment_closed_worker() {
  if (!fragment_closed_queue)
    return;

  FragmentClosedEvent *ev = g_new0(FragmentClosedEvent, 1);
  ev->ch = -1;
  g_async_queue_push(fragment_closed_queue, ev);

  if (fragment_closed_thread) {
    g_thread_join(fragment_closed_thread);
    fragment_closed_thread = NULL;
  }

  g_async_queue_unref(fragment_closed_queue);
  fragment_closed_queue = NULL;
}

static void mirrorStartVideoTimeActual(const gchar *value) {
  if (value == NULL || value[0] == '\0')
    return;

  /* 이미 값이 있으면 덮어쓰지 않음 (첫 프래그먼트만 기록) */
  gchar *existing = NULL;
  if (g_file_get_contents(CAM_STATE_RECORDING_ACTUAL_FILE, &existing, NULL, NULL)) {
    gboolean has_value = (existing[0] != '\0');
    g_free(existing);
    if (has_value)
      return;
  }

  /* 파일 기반 cam_state: 단순 파일 쓰기 */
  GError *err = NULL;
  if (!g_file_set_contents(CAM_STATE_RECORDING_ACTUAL_FILE, value, -1, &err)) {
    __LOG(LOG_ERR, "[GST][%s:%d] cam_state actual write failed: %s",
          _FILE_, __LINE__, err ? err->message : "unknown");
    if (err)
      g_error_free(err);
  }
}
// MuxSinkBin muxSinkBin[MAX_CHANNEL];
gboolean config_camera(gpointer user_data);

void handle_sigsegv(int sig) {
  void *array[10];
  size_t size;

  __LOG(LOG_CRIT, "[GST][%s:%d] Caught Segmentation Fault (signal %d)!", _FILE_, __LINE__, sig);
  
  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  
  exit(1);
}

void handle_sigint(int sig) {
  // g_print("Caught signal %d, sending EOS to pipeline\n", sig);
  __LOG(LOG_EMERG, "[GST][%s:%d] Caught signal %d, sending EOS to pipeline",
        _FILE_, __LINE__, sig);
  gst_element_send_event(pipeline, gst_event_new_eos());
}

gboolean bus_message_parse(GstBus *bus, GstMessage *message, gpointer data) {
  gchar *str = NULL;
  static guint8 cam_cnt = 0;
  static GstState state = GST_STATE_VOID_PENDING;
  GstMessageType mType = GST_MESSAGE_TYPE(message);

  // [최적화] 고주파 비관심 메시지를 switch문 진입 전 한꺼번에 필터링
  if (mType & (GST_MESSAGE_QOS | GST_MESSAGE_TAG | GST_MESSAGE_STREAM_STATUS))
    return TRUE;

  switch (mType) {
  case GST_MESSAGE_STATE_CHANGED: {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(message, &old_state, &new_state,
                                    &pending_state);
    if (state != old_state) {
      state = old_state;
      const gchar *oldn = gst_element_state_get_name(old_state);
      const gchar *newn = gst_element_state_get_name(new_state);

      str = g_strdup_printf("%s_%s_%s", gst_element_get_name(pipeline), oldn,
                            newn);
      GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                                str);
    }
    __LOG(LOG_DEBUG, "[GST][%s:%d] from %s to %s int %s", _FILE_, __LINE__,
          gst_element_state_get_name(old_state),
          gst_element_state_get_name(new_state), GST_OBJECT_NAME(message->src));
    break;
  }

  case GST_MESSAGE_ERROR: {
    GError *err;
    gchar *debug;
    gst_message_parse_error(message, &err, &debug);
    if (err) {
      __LOG(LOG_ERR, "[GST][%s:%d] err(%d) %s from element(%s)", _FILE_,
            __LINE__, err->code, err->message, GST_MESSAGE_SRC_NAME(message));
      healthProducerNotePipelineError(err->message);
      if (safe_write_file("/tmp/gst_err", err->message) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] err writing to /tmp/gst_err", _FILE_,
              __LINE__);
      g_error_free(err);
    }
    if (debug) {
      __LOG(LOG_ERR, "[GST][%s:%d] error debug : %s\n", __FILE__, __LINE__,
            (debug) ? debug : "none");
      if (safe_write_file("/tmp/gst_err", debug) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] err writing to /tmp/gst_err", _FILE_,
              __LINE__);
      g_free(debug);
    }
    gst_element_send_event(pipeline, gst_event_new_eos());
    break;
  }

  case GST_MESSAGE_EOS: {
    __LOG(LOG_EMERG, "[GST][%s:%d] GST_MESSAGE_EOS", _FILE_, __LINE__);
    if (is_interrupted) {
      cam_cnt++;
      g_main_loop_quit(loop);
    } else {
      is_interrupted = TRUE;
      g_main_loop_quit(loop);
    }
    break;
  }

  case GST_MESSAGE_ELEMENT: {
    const GstStructure *structure = gst_message_get_structure(message);
    if (gst_structure_has_name(structure, "splitmuxsink-fragment-opened")) {
      const gchar *location =
          g_value_get_string(gst_structure_get_value(structure, "location"));
      GstClockTime running_time = GST_CLOCK_TIME_NONE;
      const GValue *rv = gst_structure_get_value(structure, "running-time");
      if (rv)
        running_time = g_value_get_uint64(rv);

      GDateTime *datetime = g_date_time_new_now_local();
      if (datetime) {
        gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
        if (date_str) {
          mirrorStartVideoTimeActual(date_str);
          g_free(date_str);
        }
        g_date_time_unref(datetime);
      }

      // 해당 채널의 MuxSinkBin에 전달하여 통합 로그 출력 및 시간 기록
      ThreadArgs *tArgs = (ThreadArgs *)data;
      MuxSinkBin *msBin = (MuxSinkBin *)tArgs->arg3;
      GstElement *src_element = (GstElement *)GST_MESSAGE_SRC(message);
      for (int i = 0; i < MAX_CHANNEL; i++) {
        if (msBin[i].be.sink == src_element) {
          msBin[i].handleFragmentOpened(location, running_time);
          break;
        }
      }
    } else if (gst_structure_has_name(structure,
                                      "splitmuxsink-fragment-closed")) {
      const gchar *location =
          g_value_get_string(gst_structure_get_value(structure, "location"));
      GstClockTime running_time = GST_CLOCK_TIME_NONE;
      GstClockTime duration = GST_CLOCK_TIME_NONE;

      const GValue *rv = gst_structure_get_value(structure, "running-time");
      if (rv) running_time = g_value_get_uint64(rv);
      const GValue *dv = gst_structure_get_value(structure, "duration");
      if (dv) duration = g_value_get_uint64(dv);

      ThreadArgs *tArgs = (ThreadArgs *)data;
      MuxSinkBin *msBin = (MuxSinkBin *)tArgs->arg3;
      GstElement *src_element = (GstElement *)GST_MESSAGE_SRC(message);
      for (int i = 0; i < MAX_CHANNEL; i++) {
        if (msBin[i].be.sink == src_element) {
          if (fragment_closed_queue) {
            // [최적화] 객체 풀링 사용
            FragmentClosedEvent *ev = NULL;
            g_mutex_lock(&pool_mutex);
            for (int k = 0; k < MAX_EVENT_POOL_SIZE; k++) {
              if (!event_pool[k].in_use) {
                ev = &event_pool[k];
                ev->in_use = TRUE;
                break;
              }
            }
            g_mutex_unlock(&pool_mutex);

            if (ev) {
              ev->ch = i;
              ev->location = g_strdup(location);
              ev->running_time = running_time;
              ev->duration = duration;
              g_async_queue_push(fragment_closed_queue, ev);
            } else {
              __LOG(LOG_ERR, "[GST][%s:%d] Event pool exhausted! direct handle", _FILE_, __LINE__);
              msBin[i].handleFragmentClosed(location, running_time, duration);
            }
          } else {
            msBin[i].handleFragmentClosed(location, running_time, duration);
          }
          break;
        }
      }
    } else
      __LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__,
            gst_structure_to_string(gst_message_get_structure(message)));
    break;
  }

  case GST_MESSAGE_NEW_CLOCK: {
    if (cmdArg.stream_en[STREAM_REC] || cmdArg.audio_en) {
      GDateTime *datetime = g_date_time_new_now_local();
      gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
      if (safe_write_file(DEFAULT_START_VIDEO_TIME_PATH, date_str) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] Failed write start time", _FILE_, __LINE__);
      g_free(date_str);
      g_date_time_unref(datetime);
    }
    break;
  }

  case GST_MESSAGE_ASYNC_DONE:
  case GST_MESSAGE_STREAM_START:
    /* 최상위 파이프라인의 ASYNC_DONE 만 preroll 완료로 인정한다 (bin 도 올려보냄) */
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ASYNC_DONE &&
        pipeline != NULL && GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline))
      g_async_done_seen = TRUE;
    __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__,
          GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME(message->src));
    break;

  default:
    break;
  }

  if (str)
    g_free(str);

  return TRUE;
}

static void check_terminal_input(
    gpointer arg) //(gpointer arg0, gpointer arg1, gpointer arg2)
{
  gint bytesRead;
  gchar buffer[64];

  gint flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  ParserClass *parser = ParserClass::getInstance();

  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s start", _FILE_, __LINE__,
        __FUNCTION__);

  do {
    g_usleep(10000);

    if (is_interrupted)
      break;

    bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));

    if (bytesRead > 0) {
      // buffer[bytesRead] = '\0';
      // g_print("Input: %s", buffer);
      parser->cmd_parser(buffer, bytesRead, arg);
    }

  } while (1);

  __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s break", _FILE_, __LINE__,
        __FUNCTION__);

  return;
}

static void splitCheck(gpointer data, guint8 startSec) {
  ThreadArgs *tArgs = (ThreadArgs *)data;
  MuxSinkBin *muxSinkBin = (MuxSinkBin *)tArgs->arg3;
  EncoderBin *eBin = (EncoderBin *)tArgs->arg5;
  static gboolean start_flag = 0;
  static gboolean need_first_split = FALSE;
  static gint target_min;
  static gint64 last_split_ts = 0; // 마지막 분할 시점 (Microseconds)
  static gint last_split_min = -1; // 마지막 분할 시점의 '분'
  guint8 i;
  GDateTime *datetime = g_date_time_new_now_local();
  gint sec = g_date_time_get_second(datetime);
  gint min = g_date_time_get_minute(datetime);

  if (start_flag == 0) {
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      if ((g_link_disconnect_mask >> i) & 1) continue;  // disconnect 채널 skip
      if (muxSinkBin[i].getStartFlag() == 0) {
        g_date_time_unref(datetime);
        return;
      }
    }
    target_min = (sec != startSec) ? g_date_time_get_minute(g_date_time_add_minutes(datetime, 1)) : min;
    need_first_split = (sec != startSec);
    __LOG(LOG_NOTICE, "[GST][%s:%d] next split time : %02dm %02ds", _FILE_, __LINE__, target_min, startSec);
    start_flag = 1;
    last_split_ts = g_get_monotonic_time();
    last_split_min = min;
  }

  gint current_total_sec = min * 60 + sec;
  gint target_total_sec = target_min * 60 + startSec;
  gint diff = current_total_sec - target_total_sec;

  if (diff < -1800) diff += 3600;
  else if (diff > 1800) diff -= 3600;

  setSplitTargetEpoch(g_date_time_to_unix(datetime) - diff);

  if (diff >= 0) {
    gboolean is_fully_aligned = TRUE;
    gint splitMax = G_MININT, splitMin = G_MAXINT;
    /* [내용 기준 스큐] fragment-opened 의 running-time 은 파이프라인 공통 시간축이라
     * 채널 간 직접 비교가 된다. 집계 채널 중 하나라도 값이 없으면 rt_usable 이 꺾이고
     * 기존 벽시계 기준으로 되돌아간다. */
    GstClockTime rtMax = 0, rtMin = G_MAXUINT64;
    gboolean rt_usable = TRUE;
    gint active_count = 0;

    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      if ((g_link_disconnect_mask >> i) & 1) continue;  // disconnect 채널 skip
      gint sm = muxSinkBin[i].getSplitMsec();
      if (sm == SPLIT_MSEC_UNSET) continue;  // 새 조각 미개시 → 판단 보류
      active_count++;

      // Wrap-around 보정: 분 경계 기준 '부호 있는' 오차로 환산 (예: 59900 -> -100)
      gint signed_ms = (sm > MAX_SNAPBACK_DRIFT_MS)
                           ? (sm - MILLISECONDS_IN_MINUTE)
                           : sm;
      gint drift_ms = ABS(signed_ms);

      if (diff < 5) {
        __LOG(LOG_DEBUG, "[GST][%s:%d] ch%d sm:%dms, signed:%dms, drift:%dms, diff:%ds", _FILE_, __LINE__, i, sm, signed_ms, drift_ms, diff);
      }

      // 정시성 판단: 절대 오차가 허용 범위 이내인가?
      if (drift_ms >= cmdArg.split_max_msec) is_fully_aligned = FALSE;

      // 채널 간 스큐는 부호 있는 값으로 비교해야 분 경계에서 뒤집히지 않는다.
      // (raw 비교 시 59900 과 100 의 차이가 170ms 가 아니라 59800ms 로 계산됨)
      // 이 값은 이제 fallback 이며, 아래 running-time 기준이 우선한다.
      if (signed_ms > splitMax) splitMax = signed_ms;
      if (signed_ms < splitMin) splitMin = signed_ms;

      /* fresh 하지 않으면(= 새 조각의 fragment-opened 가 아직 안 왔으면) 이 값은
       * 이전 조각 것이다. 채널 간 조각이 섞이면 분 단위 가짜 스큐가 나오므로
       * 그 라운드는 벽시계 기준으로 되돌린다. */
      GstClockTime rt = muxSinkBin[i].getSplitRunningTime();
      if (muxSinkBin[i].isSplitRunningTimeFresh() && GST_CLOCK_TIME_IS_VALID(rt)) {
        if (rt > rtMax) rtMax = rt;
        if (rt < rtMin) rtMin = rt;
      } else {
        rt_usable = FALSE;
      }
    }

    /* 채널 간 스큐(Skew) 확인.
     * 판정 대상은 '파일 내용의 경계'이므로 running-time 을 기준으로 삼는다.
     * 벽시계 split_msec 은 파일이 열린 '시각' 이라 스트리밍 스레드 스케줄링 지연을
     * 그대로 담는다. 즉 네 채널 내용이 완전히 정렬돼 있어도 임계를 넘겨
     * 불필요한 강제 분할(스냅백)을 유발할 수 있다.
     * running-time 을 얻지 못한 채널이 있으면 기존 벽시계 기준으로 되돌린다. */
    gint wall_skew_ms = (active_count > 1) ? (splitMax - splitMin) : 0;
    gint skew_ms = wall_skew_ms;
    const gchar *skew_basis = "wall-clock";
    if (active_count > 1 && rt_usable) {
      skew_ms = (gint)((rtMax - rtMin) / GST_MSECOND);
      skew_basis = "running-time";
    }
    if (active_count > 1 && skew_ms >= cmdArg.split_diff_msec) {
      is_fully_aligned = FALSE;
    }
    /* 판정 기준을 현장에서 확인할 수 있어야 한다. 운영 log_level 은 NOTICE(5) 이므로
     * DEBUG 로 남기면 타겟에 보이지 않는다(실측 확인). 다만 매분 찍으면 로그가 더러워지므로
     * '기준이 바뀌는 순간'만 사건으로 보고 최초 1회 포함해 NOTICE 로 남긴다.
     * running-time -> wall-clock 으로 떨어지는 순간이 곧 조사 대상이다. */
    static const gchar *reported_basis = NULL;
    static gint reported_active = -1;
    if (active_count > 1 &&
        (g_strcmp0(reported_basis, skew_basis) != 0 ||
         reported_active != active_count)) {
      __LOG(LOG_NOTICE,
            "[GST][%s:%d] skew basis: %s (skew:%dms, wall-skew:%dms, active:%d)",
            _FILE_, __LINE__, skew_basis, skew_ms, wall_skew_ms, active_count);
      reported_basis = skew_basis;
      reported_active = active_count;
    }
    if (diff < 5 && active_count > 1) {
      __LOG(LOG_DEBUG,
            "[GST][%s:%d] skew:%dms (%s), wall-skew:%dms, active:%d",
            _FILE_, __LINE__, skew_ms, skew_basis, wall_skew_ms, active_count);
    }

    if (is_fully_aligned) {
      if (need_first_split) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (!cmdArg.cam[i].enable) continue;
          if ((g_link_disconnect_mask >> i) & 1) continue;
          muxSinkBin[i].splitNow(NULL, FALSE);
          muxSinkBin[i].setSplitMsec(SPLIT_MSEC_UNSET);
          muxSinkBin[i].setSplitRunningTime(GST_CLOCK_TIME_NONE);
        }
        need_first_split = FALSE;
      }
      target_min = (target_min + cmdArg.duration) % 60;
      __LOG(LOG_NOTICE, "[GST][%s:%d] All channels aligned to 00s. Next target : %02dm %02ds", 
            _FILE_, __LINE__, target_min, startSec);
      last_split_ts = g_get_monotonic_time();
      last_split_min = min;
      g_date_time_unref(datetime);
      return;
    }

    // 3. 실행 시점 결정: 기본적으로 즉시 강제 정정하되, 정각 직전만 유예
    gboolean do_force = TRUE;
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      if ((g_link_disconnect_mask >> i) & 1) continue;
      gint sm = muxSinkBin[i].getSplitMsec();
      if (sm == SPLIT_MSEC_UNSET) continue;  // 새 조각 미개시 → 유예 판단 제외
      if (sm >= SNAP_BACK_GRACE_PERIOD_MS && (diff * 1000 < cmdArg.split_max_msec)) {
        do_force = FALSE; break;
      }
    }

    // 보호 로직: 같은 분 내에서만 5초 간격 보호
    if (do_force) {
      gint64 now_ts = g_get_monotonic_time();
      if (min == last_split_min && (now_ts - last_split_ts) < (MIN_SPLIT_INTERVAL_SEC * 1000000)) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Skip forced split (Interval protection: %ldms)", 
              _FILE_, __LINE__, (long)((now_ts - last_split_ts) / 1000));
        do_force = FALSE;
      }
    }

    if (do_force) {
      /* 두 기준을 함께 남긴다: 현장 로그만으로 '실제 어긋남'과
       * '벽시계 프록시 잡음'을 사후 분리할 수 있어야 한다. */
      __LOG(LOG_ERR,
            "[GST][%s:%d] Snap-back split (diff:%ds, smMax:%dms, skew:%dms/%s, wall-skew:%dms)",
            _FILE_, __LINE__, diff, splitMax, skew_ms, skew_basis, wall_skew_ms);
      
      if (cmdArg.dual_enc == FALSE && eBin != NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (!cmdArg.cam[i].enable) continue;
          if ((g_link_disconnect_mask >> i) & 1) continue;
          eBin[i].forceKeyframe();
        }
      }

      gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
      safe_write_file(DEFAULT_START_VIDEO_TIME_PATH, date_str);
      g_free(date_str);

      for (i = 0; i < MAX_CHANNEL; i++) {
        if (!cmdArg.cam[i].enable) continue;
        if ((g_link_disconnect_mask >> i) & 1) continue;
        muxSinkBin[i].splitNow(NULL, FALSE);
        muxSinkBin[i].setSplitMsec(SPLIT_MSEC_UNSET);
        muxSinkBin[i].setSplitRunningTime(GST_CLOCK_TIME_NONE);
      }
      target_min = (target_min + cmdArg.duration) % 60;
      last_split_ts = g_get_monotonic_time();
      last_split_min = min;
    }
  }

  if (datetime) g_date_time_unref(datetime);
  return;
}

static gboolean split_timer_callback(gpointer data) {
  if (is_interrupted)
    return G_SOURCE_REMOVE;

  splitCheck(data, cmdArg.split_sec);
  return G_SOURCE_CONTINUE;
}

static gboolean setSRT(gpointer arg) {
  ThreadArgs *threadArgs = (ThreadArgs *)arg;
  RecordBin *recordBin = (RecordBin *)(threadArgs->arg1);
  RtspServerBin *rtspServerBin = (RtspServerBin *)(threadArgs->arg2);
  EncoderBin *encoderBin = (EncoderBin *)(threadArgs->arg5);
  // CaptureBin *captrueBin = (CaptureBin *)(threadArgs->arg2);
  static gint index = 0;
  guint8 i;
  gchar *text;
  // text = g_strdup_printf("2023-01-27 22:40:02 VD3001, M, A,
  // 34049/174014(1000000), 1298.5678mm/s, 300mV, (?)400mA, 80.5%/71.5%, E696,
  // Level 7, Level 4");
#ifdef TIMEOVERLAY
  text =
      g_strdup_printf("VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, "
                      "%dmV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4",
                      index++);
#else
  {
    GDateTime *overlay_datetime = g_date_time_new_now_local();
    gchar *overlay_date_str =
        g_date_time_format(overlay_datetime, "%Y-%m-%d %H:%M:%S");
    text = g_strdup_printf(
        "%s VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, "
        "(?)400mA, 80.5%%/71.5%%, E696, Level 7, Level 4",
        overlay_date_str, index++);
    g_free(overlay_date_str);
    g_date_time_unref(overlay_datetime);
  }
#endif
  //__LOG(LOG_DEBUG, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__,
  //__FUNCTION__, info->index, info->ch); g_object_set(info->timeoveraly,
  // "text", g_strdup_printf("test srt num(%d)", i++), NULL);
  for (i = 0; i < MAX_CHANNEL; i++) {
    if (cmdArg.dual_enc == TRUE) {
      if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC])
        recordBin[i].setOverlayText(text);
      if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
        rtspServerBin[i].setOverlayText(text);
    } else {
      if (cmdArg.cam[i].enable)
        encoderBin[i].setOverlayText(text);
    }
  }

  index++;

  g_free(text);

  return TRUE;
}

gint getPasswdWithAES(CmdArg *arg) {
  gint ret = 0;
  gchar passwd[1024] = {
      0,
  };
  const gchar *path = DEFAULT_PASSWD_PATH;
  AESClass *aesClass = AESClass::getInstance();

  if (aesClass->encrypt_get_passwd(path, passwd) < 0) {
    ret = aesClass->encrypt_change_passwd(path, NULL, DEFAULT_RTSP_PASSWD);
    if (ret < 0) {
      __LOG(LOG_ERR, "[CFG][%s:%d] Error change passwd .. ", _FILE_, __LINE__);
    }
    arg->rtsp_passwd = strdup(DEFAULT_RTSP_PASSWD);
  } else
    arg->rtsp_passwd = strdup(passwd);

  __LOG(LOG_INFO, "[CFG][%s:%d] id : %s, passwd : %s", _FILE_, __LINE__,
        arg->rtsp_id, arg->rtsp_passwd);

  return ret;
}

gboolean config_camera(gpointer user_data) {
  gint i = GPOINTER_TO_INT(user_data);
  guint16 ch_num0 = i * 2;
  guint16 ch_num1 = i * 2 + 1;

  if (cmdArg.cam[ch_num0].enable && cmdArg.cam[ch_num1].enable) {
    __LOG(LOG_INFO, "[CFG][%s:%d] ch%d enable, ch%d enable", _FILE_, __LINE__,
          ch_num0, ch_num1);
    int bus = i ? 1 : 2;

    /*
     * NOTE: 0x100c (AP1302 rotation) is now driver-owned via V4L2 controls.
     * Keep the legacy I2C path here for reference.
     */
    (void)bus;

    /*
    // Use safe_exec_i2c instead of system() for ch_num0 (addr 0x11)
    __LOG(LOG_INFO, "[CFG][%s:%d] ch%d i2cwrite bus=%d addr=0x11 reg=0x100c
    value=0x%04x", _FILE_, __LINE__, ch_num0, bus, (cmdArg.ch_rotate >> (i * 4))
    & 0x03); if (safe_exec_i2c("i2cwrite", bus, 0x11, 0x100c, (cmdArg.ch_rotate
    >> (i * 4)) & 0x03, NULL, 0) < 0)
        __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__,
    ch_num0);
    */

    // NOTE: AE/Gain/Exposure are now controlled via V4L2 extra-controls in
    // videoBin.cpp. Rotation is also driver-owned via V4L2 controls; legacy I2C
    // paths are disabled.

    /*
    // Use safe_exec_i2c instead of system() for ch_num1 (addr 0x12)
    __LOG(LOG_INFO, "[CFG][%s:%d] ch%d i2cwrite bus=%d addr=0x12 reg=0x100c
    value=0x%04x", _FILE_, __LINE__, ch_num1, bus, (cmdArg.ch_rotate >> (i * 4 +
    2)) & 0x03); if (safe_exec_i2c("i2cwrite", bus, 0x12, 0x100c,
    (cmdArg.ch_rotate >> (i * 4 + 2)) & 0x03, NULL, 0) < 0)
        __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__,
    ch_num1);
    */

  } else {
#define STR_LEN 8
    gchar str[STR_LEN];
    gchar val[4] = {0, 0, 0, 0};
    int bus = i ? 1 : 2;

    // Use safe_exec_i2c for i2cread instead of popen
    memset(str, 0, STR_LEN);
    if (safe_exec_i2c("i2cread", bus, 0x48, 0x0013, 1, str, STR_LEN) < 0) {
      __LOG(LOG_CRIT, "[CFG][%s:%d] i2cread bus=%d addr=0x48 reg=0x0013 failed",
            _FILE_, __LINE__, bus);
    }

    str[4] = 0;
    //__LOG(LOG_NOTICE, "[CFG][%s:%d] link byte : %s", _FILE_, __LINE__, str);

    if (strstr(str, "0xea"))
      val[ch_num0] = 1;
    else if (strstr(str, "0xda"))
      val[ch_num1] = 1;
    else
      __LOG(LOG_CRIT, "[CFG][%s:%d] csi[%d] not display", _FILE_, __LINE__, i);

    if (cmdArg.cam[ch_num0].enable & 0x01) {
      __LOG(LOG_INFO, "[CFG][%s:%d] ch%d enable, ch%d disable", _FILE_,
            __LINE__, ch_num0, ch_num1);

      /*
      // Use safe_exec_i2c instead of system() for ch_num0 (addr 0x3c)
      __LOG(LOG_INFO, "[CFG][%s:%d] ch%d i2cwrite bus=%d addr=0x3c reg=0x100c
      value=0x%04x", _FILE_, __LINE__, ch_num0, bus, (cmdArg.ch_rotate >> (i *
      4)) & 0x03); if (safe_exec_i2c("i2cwrite", bus, 0x3c, 0x100c,
      (cmdArg.ch_rotate >> (i * 4)) & 0x03, NULL, 0) < 0)
          __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__,
      ch_num0);
      */
      //if (val[ch_num0] == 0) __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display", _FILE_, __LINE__, ch_num0, ch_num1);

      // NOTE: AE/Gain/Exposure now controlled via V4L2 extra-controls in
      // videoBin.cpp Keeping rotation here as V4L2 doesn't have rotation
      // control (only hflip/vflip)
    } else if (cmdArg.cam[ch_num1].enable & 0x01) {
      __LOG(LOG_INFO, "[CFG][%s:%d] ch%d disable, ch%d enable", _FILE_,
            __LINE__, ch_num0, ch_num1);

      /*
      // Use safe_exec_i2c instead of system() for ch_num1 (addr 0x3c)
      __LOG(LOG_INFO, "[CFG][%s:%d] ch%d i2cwrite bus=%d addr=0x3c reg=0x100c
      value=0x%04x", _FILE_, __LINE__, ch_num1, bus, (cmdArg.ch_rotate >> (i * 4
      + 2)) & 0x03); if (safe_exec_i2c("i2cwrite", bus, 0x3c, 0x100c,
      (cmdArg.ch_rotate >> (i * 4 + 2)) & 0x03, NULL, 0) < 0)
          __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__,
      ch_num1);
      */
      //if (val[ch_num1] == 0) __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display", _FILE_, __LINE__, ch_num1, ch_num0);

      // NOTE: AE/Gain/Exposure now controlled via V4L2 extra-controls in
      // videoBin.cpp Keeping rotation here as V4L2 doesn't have rotation
      // control (only hflip/vflip)
    }
  }

  return G_SOURCE_REMOVE;
}

gint main(gint argc, gchar *argv[]) {
  guint major, minor, micro, nano;
  const gchar *nano_str;

  atexit(gst_deinit);
  gst_init(&argc, &argv);
  gst_version(&major, &minor, &micro, &nano);

  if (nano == 1)
    nano_str = "(CVS)";
  else if (nano == 2)
    nano_str = "(Prerelease)";
  else
    nano_str = "";
  // printf("This program i linked against Gstreamer %d.%d.%d %s\n", major,
  // minor, micro, nano_str);

  ParserClass *parser = ParserClass::getInstance();
  // cmdArg.appname = CHARNEXT(argv[0], '/');
  parser->init_arg(argv[0]);
  cmdArg = parser->arg;
  // getPasswdWithAES(&parser->arg);

  if (parser->json_parser(DEFAULT_JSON_PATH, JSON_CAM_OBJ_NAME) < 0)
    return -1;

  if (parser->arg_parser(&argc, &argv) <= 0)
    return -1;

  getPasswdWithAES(&parser->arg);
  // if(!strcmp(parser->arg.rtsp_passwd, DEFAULT_RTSP_PASSWD) == 0)

  __LOG(LOG_NOTICE,
        "[GST][%s:%d] %s version : %s linked against Gstreamer %d.%d.%d %s",
        __FILE__, __LINE__, parser->arg.appname, APP_VERSION, major, minor,
        micro, nano_str);

  cmdArg = parser->arg;
  if (parser->check_arg() < 0)
    return -1;
  // [instant-snapshot] check_arg() validates/clamps parser->arg (e.g. an out-of-range
  // --capinstant) AFTER the cmdArg copy above, and cmdArg is otherwise never re-synced, so
  // an invalid value would survive in the runtime cmdArg. Re-copy so cmdArg reflects the
  // clamped/validated values. cap_instant_enabled()'s strict check stays as defense-in-depth.
  cmdArg = parser->arg;

  Max9296PrepareInput prepare_input = {};
  prepare_input.width = static_cast<uint32_t>(cmdArg.width);
  prepare_input.height = static_cast<uint32_t>(cmdArg.height);
  for (unsigned csi = 0; csi < 2; ++csi)
    prepare_input.fps[csi] = static_cast<uint32_t>(cmdArg.main_fps[csi]);
  for (unsigned ch = 0; ch < 4; ++ch)
    prepare_input.channel_enabled[ch] = cmdArg.cam[ch].enable ? 1 : 0;
  prepare_input.generation = max9296_prepare_generate_generation();

  Max9296PrepareTarget prepare_targets[2] = {};
  if (max9296_prepare_build_targets(&prepare_input, prepare_targets) < 0) {
    __LOG(LOG_CRIT,
          "[MAX9296_PREPARE] invalid request generation=%" PRIu64
          " tuple=%ux%u fps=%u,%u enable=%u,%u,%u,%u",
          prepare_input.generation, prepare_input.width, prepare_input.height,
          prepare_input.fps[0], prepare_input.fps[1],
          prepare_input.channel_enabled[0], prepare_input.channel_enabled[1],
          prepare_input.channel_enabled[2], prepare_input.channel_enabled[3]);
    return EXIT_FAILURE;
  }

  gint app_exit_code = EXIT_SUCCESS;
  gint max9296_owner_fd =
      max9296_prepare_acquire_owner_lock(MAX9296_PREPARE_OWNER_LOCK);
  if (max9296_owner_fd < 0) {
    __LOG(LOG_CRIT, "[MAX9296_PREPARE] owner lock failed: %d",
          max9296_owner_fd);
    return EXIT_FAILURE;
  }

  if (parser->apply_camera_sysfs() < 0) {
    max9296_prepare_release_owner_lock(max9296_owner_fd);
    return EXIT_FAILURE;
  }

  // MuxBin* muxBin = MuxBin::getInstance();
  GstBus *bus;
  VideoBin videoBin[MAX_VIDEO_SRC];
  RecordBin recordBin[MAX_CHANNEL];
  RtspServerBin rtspServerBin[MAX_CHANNEL + 1];
  MuxSinkBin muxSinkBin[MAX_CHANNEL];
  CaptureBin captureBin[MAX_CHANNEL];
  EncoderBin encoderBin[MAX_CHANNEL];
  GThread *terminalThread = NULL;
  GstStateChangeReturn ret;
  guint8 i = 0;
  guint srtTimer_id = 0;
  ThreadArgs *threadArgs = g_new(ThreadArgs, 1);
  const gchar *stateChangeReturnStr[4] = {
      "GST_STATE_CHANGE_FAILURE", "GST_STATE_CHANGE_SUCCESS",
      "GST_STATE_CHANGE_ASYNC", "GST_STATE_CHANGE_NO_PREROLL"};
  CTCPServer *tcpServer = CTCPServer::getInstance();
  CIPCInsance *ipcInstance = CIPCInsance::getInstance();
  gint sd_mount_flag = 0;

  threadArgs->arg0 = videoBin;
  threadArgs->arg1 = recordBin;
  threadArgs->arg2 = rtspServerBin;
  threadArgs->arg3 = muxSinkBin;
  threadArgs->arg4 = captureBin;
  threadArgs->arg5 = encoderBin;

  start_fragment_closed_worker(muxSinkBin);

  // pipeline = gst_pipeline_new("test-pipeline");
  {
    GDateTime *pipe_datetime = g_date_time_new_now_local();
    gchar *pipe_date_str = g_date_time_format(pipe_datetime, "%Y%m%d_%H%M%S");
    gchar *pipe_name = g_strdup_printf("%s_%s", cmdArg.appname, pipe_date_str);
    pipeline = gst_pipeline_new(pipe_name);
    g_free(pipe_name);
    g_free(pipe_date_str);
    g_date_time_unref(pipe_datetime);
  }
  // pipeline2 = gst_pipeline_new(g_strdup_printf("%s2_%s", cmdArg.appname,
  // g_date_time_format(g_date_time_new_now_local(), "%Y%m%d_%H%M%S")));
  // muxBin.init();
  // g_print("width : %d\n", cmdArg.res[cmdArg.resolution_mode].height);

  attachInterruptHandlers();
  addSignalHandler();

  if (cmdArg.fault)
    fault_setup();

  g_setenv("GST_DEBUG_DUMP_DOT_DIR", cmdArg.dotDir, 1);
  // g_print("GST_DEBUG_DUMP_DOT_DIR : %s\n",
  // g_getenv("GST_DEBUG_DUMP_DOT_DIR"));

  // gboolean crop_en[2] = {FALSE, FALSE};
  guint8 csiNum;
  cmdArg.crop_en[0] = cmdArg.cam[0].enable && cmdArg.cam[1].enable;
  cmdArg.crop_en[1] = cmdArg.cam[2].enable && cmdArg.cam[3].enable;

  // TestBin audioBin;
  AudioBin audioBin;

  /* Start the server before any video or audio factory registers a mount.
   * Keeping this outside the video loop also supports an audio-only RTSP
   * configuration and avoids passing a NULL mount table to audioInit(). */
  if (cmdArg.stream_en[STREAM_RTSP]) {
    if (!rtspServerStart()) {
      __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_,
            __LINE__);
      goto main_end;
    }
  }

  for (i = 0; i < MAX_CHANNEL; i++) {
    // if(!(cmdArg.ch_enable & (0x1 << i))) continue;
    if (!cmdArg.cam[i].enable)
      continue;
    // chNum = i;
    csiNum = (i / 2);
    __LOG(LOG_INFO, "[GST][%s:%d] ch[%d] enable (CSI%d)", _FILE_, __LINE__, i, csiNum);
    
    // [CSI 기반 초기화 방어] 해당 CSI 드라이버가 실패하면 관련 모든 구성을 건너뜀
    if (!videoBin[csiNum].init(csiNum)) {
      __LOG(LOG_CRIT, "[GST][%s:%d] csi%d init failed. Disabling all related channels.", _FILE_, __LINE__, csiNum);
      // 해당 CSI에 속한 채널들의 설정을 강제로 비활성화하여 하부 구성 방지
      cmdArg.cam[csiNum*2].enable = FALSE;
      cmdArg.cam[csiNum*2+1].enable = FALSE;
      continue; 
    }
// #if !defined(CHANNEL_EACH_CROP)
#ifndef CHANNEL_EACH_CROP
    videoBin[i / 2].addCrop((CropDir)(i % 2));
#endif
    if (cmdArg.stream_en[STREAM_CAP]) {
      if (!videoBin[csiNum].addBinCaptureSrcPad(i)) {
        __LOG(LOG_CRIT,
              "[GST][%s:%d] ch%d capture pad add err in csi%d video bin",
              _FILE_, __LINE__, i, csiNum);
        goto main_end;
      }

      // captureBin[i].setAppsrc(recordBin[chNum].getBinAppsrc());

      if (1) //(cmdArg.cap_always)
      {
        if (!captureBin[i].init(i)) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d capture bin init err", _FILE_,
                __LINE__, i);
          goto main_end;
        }

        if (!captureBin[i].addBinToPipe(pipeline)) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d captrue bin add err", _FILE_,
                __LINE__, i);
          goto main_end;
        }

        if (gst_pad_link(videoBin[csiNum].getBinCaptureSrcPad(i),
                         captureBin[i].getBinSinkPad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d capture pad link err", _FILE_,
                __LINE__, i);
          // return -1;
          goto main_end;
        }
      }
    }

    // json_parser(DEFAULT_JSON_PATH);
    // print_option();
    if (cmdArg.dual_enc == FALSE &&
        (cmdArg.stream_en[STREAM_REC] || cmdArg.stream_en[STREAM_RTSP])) {
      if (!encoderBin[i].init(i)) {
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d init err in encoderBin. Aborting.", _FILE_,
              __LINE__, i);
        goto main_end;
      }

      if (!videoBin[csiNum].addBinRecordSrcPad(i)) {
        __LOG(LOG_CRIT,
              "[GST][%s:%d] ch%d record pad add err in csi%d video bin", _FILE_,
              __LINE__, i, csiNum);
        goto main_end;
      }

      if (gst_pad_link(videoBin[csiNum].getBinRecordSrcPad(i),
                       encoderBin[i].getBinSinkPad()) != GST_PAD_LINK_OK) {
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record pad link err", _FILE_,
              __LINE__, i);
        goto main_end;
      }
    }

    if (cmdArg.stream_en[STREAM_REC]) {
      if (!muxSinkBin[i].init(i)) {
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink init err. Aborting.", _FILE_,
              __LINE__, i);
        goto main_end;
      }

      if (cmdArg.dual_enc == TRUE) {
        if (!recordBin[i].init(i, cmdArg.crop_en[csiNum])) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record init err", _FILE_, __LINE__,
                i, csiNum);
          goto main_end;
        }

        if (!videoBin[csiNum].addBinRecordSrcPad(i)) {
          __LOG(LOG_CRIT,
                "[GST][%s:%d] ch%d record pad add err in csi%d video bin",
                _FILE_, __LINE__, i, csiNum);
          goto main_end;
        }

        if (gst_pad_link(videoBin[csiNum].getBinRecordSrcPad(i),
                         recordBin[i].getBinSinkPad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }

        if (gst_pad_link(recordBin[i].getBinSrcPad(),
                         muxSinkBin[i].getBinQueuePad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }
      } else {
        if (!encoderBin[i].addBinRecSrcPad()) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record pad add err in encoderBin",
                _FILE_, __LINE__, i);
          goto main_end;
        }

        if (gst_pad_link(encoderBin[i].getBinRecSrcPad(),
                         muxSinkBin[i].getBinQueuePad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }
      }
      // g_thread_new("split-timer-thread", (GThreadFunc)splitTimerStart,
      // &muxSinkBin[chNum]);
    }

    if (cmdArg.stream_en[STREAM_RTSP]) {
      if (!rtspServerBin[i].init(i, cmdArg.crop_en[csiNum])) {
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad link err", _FILE_, __LINE__,
              i);
        goto main_end;
      }

      if (cmdArg.dual_enc == TRUE) {
        if (!videoBin[csiNum].addBinRtspSrcPad(i)) {
          __LOG(LOG_CRIT,
                "[GST][%s:%d] ch%d rtsp pad add err in csi%d video bin", _FILE_,
                __LINE__, i, csiNum);
          goto main_end;
        }

        if (gst_pad_link(videoBin[csiNum].getBinRtspSrcPad(i),
                         rtspServerBin[i].getBinSinkPad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }
      } else {
        if (!encoderBin[i].addBinRtspSrcPad()) {
          __LOG(LOG_CRIT,
                "[GST][%s:%d] ch%d rtsp pad add err in csi%d encoderBin",
                _FILE_, __LINE__, i, csiNum);
          goto main_end;
        }

        if (gst_pad_link(encoderBin[i].getBinRtspSrcPad(),
                         rtspServerBin[i].getBinSinkPad()) != GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }
      }
      // else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_,
      // __LINE__, chNum);
    }

  }

  /* AudioBin is shared by every recording mux and the audio RTSP mount.  It
   * must be constructed once, after all enabled video/mux bins exist; doing
   * this in the channel loop rebuilt the same named elements for every camera
   * and prevented the H.265 + audio configuration from reaching PLAYING. */
  if (cmdArg.audio_en) {
    if (!audioBin.init()) {
      __LOG(LOG_CRIT, "[GST][%s:%d] audio bin init err", _FILE_, __LINE__);
      goto main_end;
    }

    if (cmdArg.stream_en[STREAM_REC]) {
      for (i = 0; i < MAX_CHANNEL; i++) {
        if (!cmdArg.cam[i].enable)
          continue;

        if (!audioBin.addBinSrcPad(i)) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio src pad add err", _FILE_,
                __LINE__, i);
          goto main_end;
        }

        if (!muxSinkBin[i].addBinAudioSinkPad()) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio sink pad add err", _FILE_,
                __LINE__, i);
          goto main_end;
        }

        if (gst_pad_link(audioBin.getBinSrcPad(i),
                         muxSinkBin[i].getBinAudioSinkPad()) !=
            GST_PAD_LINK_OK) {
          __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio pad link err", _FILE_,
                __LINE__, i);
          goto main_end;
        }
        __LOG(LOG_INFO, "[GST][%s:%d] ch%d audio pad link", _FILE_, __LINE__,
              i);
      }
    }

    if (cmdArg.stream_en[STREAM_RTSP]) {
      if (!rtspServerBin[4].audioInit()) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp audio init err", _FILE_, __LINE__);
        goto main_end;
      }
      if (!audioBin.addBinSrcPad(4)) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp audio src pad add err", _FILE_,
              __LINE__);
        goto main_end;
      }
      if (gst_pad_link(audioBin.getBinSrcPad(4),
                       rtspServerBin[4].getBinSinkPad()) != GST_PAD_LINK_OK) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp audio pad link err", _FILE_,
              __LINE__);
        goto main_end;
      }
    }
  }

  // Enable timestamp debug at startup if requested via argv.
  if (cmdArg.dbg_cap_ts && cmdArg.stream_en[STREAM_CAP]) {
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (cmdArg.cam[i].enable) {
        captureBin[i].setTimeStampDebug();
      }
    }
  }
  if (cmdArg.dbg_rtsp_ts && cmdArg.stream_en[STREAM_RTSP]) {
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (cmdArg.cam[i].enable) {
        rtspServerBin[i].setTimeStampDebug();
      }
    }
    if (cmdArg.audio_en) {
      rtspServerBin[4].setTimeStampDebug();
    }
  }

  // gst_element_set_state(pipeline, GST_STATE_PLAYING);
  // signal(SIGINT, handle_sigint);
  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                            gst_element_get_name(pipeline));
  // GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(pipeline),
  // GST_DEBUG_GRAPH_SHOW_VERBOSE, gst_element_get_name(pipeline));

  gst_pipeline_use_clock(GST_PIPELINE(pipeline), gst_system_clock_obtain());
  // gst_pipeline_use_clock(GST_PIPELINE(pipeline),
  // gst_element_get_clock(videoBin[0].be.src));
  // gst_pipeline_use_clock(GST_PIPELINE(pipeline),
  // gst_element_get_clock(audioBin.be.src));

  // gst_element_set_base_time(pipeline, 0);
  // gst_element_set_start_time(pipeline, GST_CLOCK_TIME_NONE);

  bus = gst_element_get_bus(pipeline);
  if (!bus) {
    __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_,
          __LINE__);
    goto main_end;
  }

  gst_bus_add_watch(bus, bus_message_parse, threadArgs);

  gst_object_unref(bus);

  /* prepare_input was preflighted before sysfs apply. Re-copy only channel
   * enables because VideoBin::init failure may disable a CSI domain. */
  for (unsigned ch = 0; ch < 4; ++ch)
    prepare_input.channel_enabled[ch] = cmdArg.cam[ch].enable ? 1 : 0;

  {
    Max9296PrepareReport prepare_report = {};
    gint prepare_ret =
        max9296_prepare_all(&prepare_input, &prepare_report, NULL);
    const gint prepare_log_level =
        (prepare_ret < 0 || prepare_report.legacy_fallback) ? LOG_CRIT
                                                           : LOG_NOTICE;

    for (unsigned csi = 0; csi < 2; ++csi) {
      const uint32_t enable = prepare_input.channel_enabled[csi * 2] |
                              (prepare_input.channel_enabled[csi * 2 + 1] << 1);
      const uint32_t tuple_width =
          enable == 3 ? prepare_input.width * 2 : prepare_input.width;
      const Max9296PrepareDomainReport &domain = prepare_report.domain[csi];
      __LOG(prepare_log_level,
            "[MAX9296_PREPARE] generation=%" PRIu64
            " CSI%u path=%s tuple=%ux%u@%u enable=%u action=%d"
            " elapsed_ms=%" PRIu64
            " primary_errno=%d rollback_errno=%d before_state=%d"
            " after_state=%d%s",
            prepare_input.generation, csi, max9296_prepare_path(csi),
            tuple_width, prepare_input.height, prepare_input.fps[csi], enable,
            static_cast<int>(domain.action), domain.elapsed_ns / 1000000ULL,
            domain.error, domain.rollback_error,
            static_cast<int>(domain.before.state),
            static_cast<int>(domain.after.state),
            prepare_report.legacy_fallback ? " LEGACY_NO_ABI" : "");
    }

    if (prepare_ret < 0) {
      app_exit_code = EXIT_FAILURE;
      goto main_end;
    }
  }

  if (cmdArg.play_delay) {
    ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);

    __LOG(LOG_INFO, "[GST][%s:%d] paused : %s", _FILE_, __LINE__,
          stateChangeReturnStr[ret]);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state paused error", _FILE_,
            __LINE__);
      gst_object_unref(pipeline);
      goto main_end;
    } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
      is_live = TRUE;
      //__LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state paused", _FILE_,
      //__LINE__);
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] delay %d sec for play", __FILE__, __LINE__,
          cmdArg.play_delay);
    sleep(cmdArg.play_delay);
  }

#if 1
  for (i = 0; i < MAX_VIDEO_SRC; i++) {
    if (videoBin[i].be.bin != NULL) {
      g_timeout_add(cmdArg.config_delay * 1000, config_camera,
                    GINT_TO_POINTER(i));
    }
  }
#endif

  // delay 후 link_status 1차 확인. 이 시점은 아직 STREAMON 전이라 대부분 미확인(-1)
  // 으로 나오며, 확정값은 PLAYING 이후 link_status_recheck_cb 가 갱신한다.
  update_link_disconnect_mask(videoBin, "pre-playing");

  sd_mount_flag = check_sd_mount_flag();
  if (sd_mount_flag == 0) {
    cmdArg.mntDir = FALLBACKDIR;
    __LOG(LOG_NOTICE, "[GST][%s:%d] sd card no mount...file dir fallback : %s",
          _FILE_, __LINE__, FALLBACKDIR);
  }

  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  __LOG(LOG_NOTICE, "[GST][%s:%d] playing : %s", _FILE_, __LINE__,
        stateChangeReturnStr[ret]);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state playing error", _FILE_,
          __LINE__);
    gst_object_unref(pipeline);
    goto main_end;
  }

  /* link_status 는 STREAMON 때 드라이버가 채우므로 PLAYING 이후에 다시 읽어야
   * 확정값이 나온다. 그리고 preroll 이 끝나지 않는 실패는 지금까지 로그의
   * '부재' 로만 드러났으므로 능동적으로 감시한다. */
  g_timeout_add_seconds(LINK_STATUS_RECHECK_SEC, link_status_recheck_cb,
                        videoBin);
  g_timeout_add_seconds(PLAYING_WATCH_SEC, playing_watch_cb, NULL);

  /* camera health v1 producer. 관측만 하며 파이프라인을 바꾸지 않는다. */
  healthProducerStart();

  if (cmdArg.input_en) {
    terminalThread = g_thread_new(
        "terminal-thread", (GThreadFunc)check_terminal_input, threadArgs);
    if (!terminalThread) {
      __LOG(LOG_CRIT, "[GST][%s:%d] terminal thread create failed", _FILE_,
            __LINE__);
    }
  }

  if (cmdArg.stream_en[STREAM_REC] || cmdArg.audio_en) {
    __LOG(LOG_INFO, "[GST][%s:%d] split timer start (500ms interval)", _FILE_,
          __LINE__);
    g_timeout_add(500, (GSourceFunc)split_timer_callback, threadArgs);
  }

  if (cmdArg.overlay_en) {
    srtTimer_id = g_timeout_add(100, (GSourceFunc)setSRT, threadArgs);
  }

  if (cmdArg.tcp_en) {
    tcpServer->init(threadArgs);
  }

  if (cmdArg.ipc_en) {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ipc enable", _FILE_, __LINE__);
    ipcInstance->init(threadArgs);
  }

  loop = g_main_loop_new(NULL, FALSE);

  if (!loop) {
    __LOG(LOG_CRIT, "[GST][%s:%d] mainLoop create error", _FILE_, __LINE__);
  } else {
    __LOG(LOG_INFO, "[GST][%s:%d] mainLoop start", _FILE_, __LINE__);
    g_main_loop_run(loop);
  }
  //__LOG(LOG_INFO, "[GST][%s:%d] Main loop exit", _FILE_, __LINE__);

main_end:
  __LOG(LOG_INFO, "[GST][%s:%d] main loop end", _FILE_, __LINE__);

  stop_fragment_closed_worker();

  if (threadArgs)
    g_free(threadArgs);

  /* 1) Terminal thread 종료 (stdin poll 중지) */
  if (terminalThread) {
    __LOG(LOG_INFO, "[GST][%s:%d] joining terminal thread...", __FILE__, __LINE__);
    g_thread_join(terminalThread);
    g_thread_unref(terminalThread);
  }
  if (srtTimer_id) {
    g_source_remove(srtTimer_id);
  }

  /* 2) RTSP 서버 중지 (클라이언트 세션 정리) */
  __LOG(LOG_INFO, "[GST][%s:%d] stopping RTSP server...", __FILE__, __LINE__);
  rtspServerStop();

  /* 3) Pipeline EOS 전송 후 타임아웃 대기 */
  __LOG(LOG_INFO, "[GST][%s:%d] sending EOS to pipeline...", _FILE_,
        __LINE__);
  gst_element_send_event(pipeline, gst_event_new_eos());

  /* 3) Pipeline NULL 전환 (3초 타임아웃) */
  gst_element_set_state(pipeline, GST_STATE_NULL);
  {
    GstState state;
    GstStateChangeReturn ret;
    ret = gst_element_get_state(pipeline, &state, NULL, 3 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_SUCCESS) {
      __LOG(LOG_INFO, "[GST][%s:%d] Pipeline unset successfully", _FILE_,
            __LINE__);
    } else {
      __LOG(LOG_CRIT, "[GST][%s:%d] Pipeline state change timeout, forcing exit",
            _FILE_, __LINE__);
    }
  }

  if (pipeline) {
    __LOG(LOG_NOTICE, "[GST][%s:%d] unreferencing pipeline...", __FILE__, __LINE__);
    gst_object_unref(pipeline);
    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline unreferenced", __FILE__, __LINE__);
  }

  if (cmdArg.tcp_en) {
    __LOG(LOG_INFO, "[GST][%s:%d] destroying TCP server...", __FILE__, __LINE__);
    tcpServer->destroy();
  }

  if (cmdArg.ipc_en) {
    ipcInstance->destroy();
  }

  if (loop) {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] g_main_loop_unref", _FILE_, __LINE__);
    g_main_loop_unref(loop);
  }

  removeSignalHandler();

  max9296_prepare_release_owner_lock(max9296_owner_fd);

  //__LOG(LOG_NOTICE, "[GST][%s:%d] exit", _FILE_, __LINE__);
  exit(app_exit_code);
}
