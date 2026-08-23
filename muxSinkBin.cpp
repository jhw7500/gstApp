/*
 *
 * Cantops muxSinkBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "muxSinkBin.h"
#include <fcntl.h>
#include <string.h>

static GstPadProbeReturn
drop_no_pts_probe_mux(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf && !GST_BUFFER_PTS_IS_VALID(buf)) {
        guint8 *ch = (guint8 *)user_data;
        __LOG(LOG_INFO, "[GST][%s:%d] ch%d dropping buffer without PTS before muxer",
              _FILE_, __LINE__, ch ? *ch : -1);
        return GST_PAD_PROBE_DROP;
    }
    return GST_PAD_PROBE_OK;
}
#include <sys/file.h>
#include <unistd.h>

// 다채널 완료 추적 구조체
typedef struct _RecordingSession {
  gchar *timestamp; // "20260127_143000"
  gboolean ch0_done;
  gboolean ch1_done;
  gboolean ch2_done;
  gboolean ch3_done;
  gboolean srt_done;
  GMutex mutex; // 스레드 안전성
} RecordingSession;

// 전역 세션 테이블 (타임스탬프 -> RecordingSession)
static GHashTable *recording_sessions = NULL;
static GMutex sessions_mutex;

// 파일명 및 타임스탬프 문자열 캐시 (B항목 최적화)
static GMutex timestamp_cache_mutex;
static gint64 last_cache_unix_sec = -1;
/* 이번 세션(분)의 마커를 이미 기록했는지. timestamp_cache_mutex 로 보호한다. */
static gint64 last_marked_epoch = -1;
static gchar cached_date_str[32];      // YYYYMMDD_HHMM00
static gchar cached_timestamp_str[32]; // YYYYMMDD_HHMM
G_LOCK_DEFINE_STATIC(split_target_lock);
static gint64 split_target_epoch_sec = 0;

void setSplitTargetEpoch(gint64 epoch_sec) {
  G_LOCK(split_target_lock);
  split_target_epoch_sec = epoch_sec;
  G_UNLOCK(split_target_lock);
}

gint64 getSplitTargetEpoch() {
  gint64 epoch_sec;
  G_LOCK(split_target_lock);
  epoch_sec = split_target_epoch_sec;
  G_UNLOCK(split_target_lock);
  return epoch_sec;
}

// 세션 관리 함수
static RecordingSession *get_or_create_session(const gchar *timestamp) {
  g_mutex_lock(&sessions_mutex);

  if (!recording_sessions) {
    recording_sessions =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }

  RecordingSession *session =
      (RecordingSession *)g_hash_table_lookup(recording_sessions, timestamp);
  if (!session) {
    session = g_new0(RecordingSession, 1);
    session->timestamp = g_strdup(timestamp);
    g_mutex_init(&session->mutex);
    g_hash_table_insert(recording_sessions, g_strdup(timestamp), session);
    __LOG(LOG_INFO, "[GST][%s:%d] Created new session: %s", _FILE_, __LINE__,
          timestamp);
  }

  g_mutex_unlock(&sessions_mutex);
  return session;
}

// Note: check_all_channels_done 함수는 on_fragment_closed에서 직접 카운팅하므로
// 제거됨

static void mark_session_complete(const gchar *timestamp) {
  // /tmp/session_<timestamp>.all_done 파일 생성 (idempotent)
  // startup alignment 미수렴 시 같은 timestamp에 두 번 도달할 수 있으므로
  // 이미 .all_done이 있으면 chk_cam_operate가 처리 중이라고 보고 부분 마커만 정리한다.
  gchar *done_file = g_strdup_printf("/tmp/session_%s.all_done", timestamp);
  gchar *video_flag = g_strdup_printf("/tmp/session_%s.video_done", timestamp);
  gchar *srt_flag = g_strdup_printf("/tmp/session_%s.srt_done", timestamp);

  if (access(done_file, F_OK) == 0) {
    __LOG(LOG_INFO, "[GST][%s:%d] all_done already exists for %s, skip recreate (idempotent)",
          _FILE_, __LINE__, timestamp);
    unlink(video_flag);
    unlink(srt_flag);
    g_free(done_file);
    g_free(video_flag);
    g_free(srt_flag);
    return;
  }

  FILE *fp = fopen(done_file, "w");
  if (fp) {
    fprintf(fp, "%s\n", timestamp);
    fclose(fp);
    __LOG(LOG_NOTICE, "[GST][%s:%d] Session complete: %s", _FILE_, __LINE__,
          timestamp);
  } else {
    __LOG(LOG_ERR, "[GST][%s:%d] Failed to create done file: %s", _FILE_,
          __LINE__, done_file);
  }

  // 개별 플래그 정리
  unlink(video_flag);
  unlink(srt_flag);

  g_free(done_file);
  g_free(video_flag);
  g_free(srt_flag);
}

static void check_and_mark_all_done(const gchar *timestamp) {
  gchar *video_flag = g_strdup_printf("/tmp/session_%s.video_done", timestamp);
  gchar *srt_flag = g_strdup_printf("/tmp/session_%s.srt_done", timestamp);

  gboolean video_done = (access(video_flag, F_OK) == 0);
  gboolean srt_done = !cmdArg.srt_en || (access(srt_flag, F_OK) == 0);

  if (video_done && srt_done) {
    mark_session_complete(timestamp);
  }

  g_free(video_flag);
  g_free(srt_flag);
}

void MuxSinkBin::handleFragmentOpened(const gchar *location, GstClockTime running_time) {
  // 이전 파일 통계 정보가 있는지 확인하여 통합 로그 출력 (time: 새 파일 시작, duration: 이전 파일 길이)
  // GST_TIME_ARGS는 나노초까지 출력하므로 ms까지 수동 포맷팅 (H:MM:SS.mmm)
  if (GST_CLOCK_TIME_IS_VALID(muxSinkData.last_end_time)) {
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d Fragment opened: %s, time: %u:%02u:%02u.%03u, duration: %u:%02u:%02u.%03u",
          _FILE_, __LINE__, muxSinkData.ch, location,
          (guint) (running_time / (GST_SECOND * 60 * 60)),
          (guint) ((running_time / (GST_SECOND * 60)) % 60),
          (guint) ((running_time / GST_SECOND) % 60),
          (guint) ((running_time / GST_MSECOND) % 1000),
          (guint) (muxSinkData.last_duration / (GST_SECOND * 60 * 60)),
          (guint) ((muxSinkData.last_duration / (GST_SECOND * 60)) % 60),
          (guint) ((muxSinkData.last_duration / GST_SECOND) % 60),
          (guint) ((muxSinkData.last_duration / GST_MSECOND) % 1000));
  } else {
    __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d Fragment opened: %s, time: %u:%02u:%02u.%03u",
          _FILE_, __LINE__, muxSinkData.ch, location,
          (guint) (running_time / (GST_SECOND * 60 * 60)),
          (guint) ((running_time / (GST_SECOND * 60)) % 60),
          (guint) ((running_time / GST_SECOND) % 60),
          (guint) ((running_time / GST_MSECOND) % 1000));
  }

  // 시작 시간 기록
  muxSinkData.last_running_time = running_time;

  /* [채널 간 스큐 판정용] splitCheck() 는 이 값으로 채널 간 어긋남을 잰다.
   * 파일이 열린 벽시계(split_msec)가 아니라 조각 첫 내용의 running-time 이므로,
   * 스트리밍 스레드 스케줄링 지연이 판정에 섞이지 않는다. */
  muxSinkData.split_running_time = running_time;
  /* 이 조각의 running-time 이 도착했다 -> 비교에 쓸 수 있다 */
  g_atomic_int_set(&muxSinkData.split_rt_stale, 0);
}

void MuxSinkBin::handleFragmentClosed(const gchar *location, GstClockTime running_time, GstClockTime duration) {
  // MuxSinkData *info = (MuxSinkData *)user_data;
  // this->muxSinkData를 사용

  GstClockTime calculated_duration = duration;
  if (!GST_CLOCK_TIME_IS_VALID(calculated_duration) && GST_CLOCK_TIME_IS_VALID(muxSinkData.last_running_time) && GST_CLOCK_TIME_IS_VALID(running_time)) {
    if (running_time > muxSinkData.last_running_time) {
      calculated_duration = running_time - muxSinkData.last_running_time;
    }
  }

  // [통계 저장] 이전 파일의 정보를 메모리에 보관
  muxSinkData.last_end_time = running_time;
  muxSinkData.last_duration = calculated_duration;

  // 다음 파일의 시작점으로 업데이트 (중요!)
  muxSinkData.last_running_time = running_time;

  // 타임스탬프 추출 (예: /mnt/sd_cam/tmp/VD3001_20260127_143000-ch0.mp4.part)
  const gchar *filename = strrchr(location, '/');
  if (!filename)
    filename = location;
  else
    filename++; // '/' 다음 문자부터

  // 타임스탬프 파싱 개선: 파일명 끝부분의 패턴(-chX)을 기준으로 역방향 추출
  // 예: .../VD3001_20260127_143000-ch0.mp4.part
  gchar *timestamp = NULL;
  const gchar *ch_ptr = strstr(filename, "-ch");

  if (ch_ptr && (ch_ptr - filename) >= 15) {
    // -ch 바로 앞 15글자 중 앞 13글자(YYYYMMDD_HHMM)만 추출
    const gchar *ts_start = ch_ptr - 15;
    if (g_ascii_isdigit(ts_start[0])) {
      timestamp = g_strndup(ts_start, 13);
    }
  }

  if (!timestamp) {
    // Fallback
    const gchar *underscore = strchr(filename, '_');
    if (underscore && strlen(underscore) > 13) {
      timestamp = g_strndup(underscore + 1, 13);
    }
  }

  if (!timestamp) {
    __LOG(LOG_ERR, "[GST][%s:%d] Failed to extract timestamp from: %s", _FILE_,
          __LINE__, location);
    return;
  }

  __LOG(LOG_INFO, "[GST][%s:%d] ch%d Extracted timestamp: %s", _FILE_, __LINE__,
        muxSinkData.ch, timestamp);

  // 세션 조회 또는 생성
  RecordingSession *session = get_or_create_session(timestamp);

  g_mutex_lock(&session->mutex);

  // 채널별 완료 표시
  switch (muxSinkData.ch) {
  case 0:
    session->ch0_done = TRUE;
    break;
  case 1:
    session->ch1_done = TRUE;
    break;
  case 2:
    session->ch2_done = TRUE;
    break;
  case 3:
    session->ch3_done = TRUE;
    break;
  default:
    __LOG(LOG_WARNING, "[GST][%s:%d] Unknown channel: %d", _FILE_, __LINE__,
          muxSinkData.ch);
    break;
  }

  __LOG(LOG_INFO, "[GST][%s:%d] Session %s status: ch0=%d ch1=%d ch2=%d ch3=%d",
        _FILE_, __LINE__, timestamp, session->ch0_done, session->ch1_done,
        session->ch2_done, session->ch3_done);

  // 모든 활성 채널 완료 확인
  // TODO: JSON에서 활성 채널 읽어서 정확히 판단
  // 임시: 완료된 채널이 2개 이상이면 세션 완료로 간주 (실제로는 설정 기반으로)
  gint completed_channels = 0;
  if (session->ch0_done)
    completed_channels++;
  if (session->ch1_done)
    completed_channels++;
  if (session->ch2_done)
    completed_channels++;
  if (session->ch3_done)
    completed_channels++;

  // 활성 채널 수 확인 (cmdArg 기반)
  gint active_channels = 0;
  for (int i = 0; i < MAX_CHANNEL; i++) {
    if (cmdArg.cam[i].enable) {
      active_channels++;
    }
  }

  __LOG(LOG_INFO, "[GST][%s:%d] Session %s progress: %d/%d (active)", _FILE_,
        __LINE__, timestamp, completed_channels, active_channels);

  // 디버그 로그: /tmp/session_debug.log에 기록
  FILE *fp_dbg = fopen("/tmp/session_debug.log", "a");
  if (fp_dbg) {
    fprintf(
        fp_dbg,
        "TS: %s, File: %s, Active: %d, Done: %d, ch0:%d ch1:%d ch2:%d ch3:%d\n",
        timestamp, filename, active_channels, completed_channels,
        session->ch0_done, session->ch1_done, session->ch2_done,
        session->ch3_done);
    fclose(fp_dbg);
  }

  // 활성 채널 수만큼 완료되었는지 확인
  if (completed_channels >= active_channels && active_channels > 0) {
    // .video_done 플래그 생성 (idempotent)
    // startup alignment 미수렴 시 같은 minute timestamp에 splitNow가 두 번 발생할 수 있다.
    // hash_table_remove로 session이 지워졌어도 두 번째 cycle이 새 session으로 누적되어
    // 이 분기에 두 번째로 도달할 수 있다. video_done이 이미 있으면 중복 생성/매칭을 차단한다.
    gchar *video_flag = g_strdup_printf("/tmp/session_%s.video_done", timestamp);
    if (access(video_flag, F_OK) != 0) {
      FILE *fp = fopen(video_flag, "w");
      if (fp)
        fclose(fp);

      // 전체 완료 확인 및 마커 생성 시도
      check_and_mark_all_done(timestamp);
    } else {
      __LOG(LOG_INFO,
            "[GST][%s:%d] video_done exists for %s (duplicate fragment_closed in same minute), skip",
            _FILE_, __LINE__, timestamp);
    }
    g_free(video_flag);

    // [P1 이슈 해결] 세션 즉시 삭제 시의 경합 방지
    // .all_done이 생성되지 않았더라도(SRT 대기 중), 영상 쪽 처리는 끝났으므로 
    // 메모리 세션은 정리하되 파일 플래그(.video_done)가 상태를 유지함.
    // vcm이 나중에 완료되면 vcm의 check_and_mark_all_done이 .all_done을 생성할 것임.
    g_mutex_unlock(&session->mutex);
    g_mutex_lock(&sessions_mutex);
    g_hash_table_remove(recording_sessions, timestamp);
    g_mutex_unlock(&sessions_mutex);
  } else {
    g_mutex_unlock(&session->mutex);
  }

  g_free(timestamp);
}

static void sink_added(GstElement *sink, GstElement *arg0, gpointer data) {
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

static void muxer_added(GstElement *splitmux, GstElement *muxer,
                        gpointer user_data) {
  const char *mode = (const char *)user_data; // "mp4" | "qt" | "ts"
  const char *type = G_OBJECT_TYPE_NAME(muxer);

  __LOG(LOG_INFO, "[GST][%s:%d] muxer-added: mode=%s, type=%s, name=%s", _FILE_,
        __LINE__, mode, type, GST_ELEMENT_NAME(muxer));

  // qtmux(GstQTMux) / mp4mux(GstMP4Mux) / mpegtsmux(GstMpegTsMux)
  if (!g_strcmp0(mode, "qt")) {
    // qtmux 기반 fMP4/robust 목적
    if (g_str_has_suffix(type, "QTMux") || g_strrstr(type, "QTMux")) {
      g_object_set(
          muxer,
          // 1초 단위 fragment (ms)
          "fragment-duration", 1000,

          // robust muxing (ns) - 필요에 따라 튜닝
          "reserved-max-duration", (guint64)(60 * GST_MSECOND),
          "reserved-moov-update-period", (guint64)(1 * GST_MSECOND),

          // streamable은 플레이어/환경 따라 득실 있으니 기본은 false 추천
          "streamable", FALSE, NULL);
    } else {
      __LOG(LOG_WARNING, "[GST] qt mode인데 qtmux가 아닌 muxer가 붙음: %s",
            type);
    }
  } else if (!g_strcmp0(mode, "mp4")) {
    // mp4mux는 옵션이 적거나 이름이 다를 수 있음. 존재하는 속성만 set.
    // 속성이 없으면 g_object_set에서 경고가 날 수 있으니, 속성 존재 여부를
    // 검사하는 습관이 좋음. 여기선 대표적으로 streamable/fragment-duration이
    // 있으면 적용하는 형태로 작성.
#if 0
        // fragment-duration이 지원되면 fMP4처럼 동작 가능
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "fragment-duration")) {
            g_object_set(muxer, "fragment-duration", 1000, NULL);
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "streamable")) {
            g_object_set(muxer, "streamable", FALSE, NULL);
        }

        // robust 관련은 mp4mux가 지원 안 할 수 있음(qtmux에 집중되어 있는 경우가 많음)
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "reserved-max-duration")) {
            g_object_set(muxer, "reserved-max-duration", (guint64)(60 * GST_SECOND), NULL);
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(muxer), "reserved-moov-update-period")) {
            g_object_set(muxer, "reserved-moov-update-period", (guint64)GST_SECOND, NULL);
        }
#endif
  } else if (!g_strcmp0(mode, "ts")) {
    // TS는 보통 별도 옵션 적음. 필요시 alignment/PCR 관련 등만.
    // 여기서는 붙은 것만 확인.
    if (g_strrstr(type, "MpegTsMux") == NULL &&
        g_strrstr(type, "MPEGTSMux") == NULL) {
      __LOG(LOG_WARNING, "[GST] ts mode : mpegtsmux? %s", type);
    }
  }
}

static gboolean handle_eos_event(GstPad *pad, GstPadProbeInfo *info,
                                 gpointer user_data) {
  MuxSinkData *data = (MuxSinkData *)user_data;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
  GstEventType eventType = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info));

  __LOG(LOG_DEBUG, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__,
        data->ch, gst_event_type_get_name(eventType));
  switch (eventType) {
  case GST_EVENT_EOS: {
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d Received EOS event on pad : %s", _FILE_,
          __LINE__, data->ch, GST_PAD_NAME(pad));
    is_interrupted = TRUE;
    break;
  }
  case GST_EVENT_TAG: {
    // GstTagList *taglist;
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d GST_EVENT_TAG", _FILE_, __LINE__,
    //data->ch); gst_event_parse_tag (event, &taglist); gst_tag_list_foreach
    // (taglist, print_tag, NULL); gst_tag_list_free (taglist);
    break;
  }
  case GST_EVENT_LATENCY: {
    GstClockTime latency;
    gst_event_parse_latency(event, &latency);
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d latency : %" GST_TIME_FORMAT "", _FILE_,
          __LINE__, data->ch, GST_TIME_ARGS(latency));
    break;
  }
  case GST_EVENT_CAPS: {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__,
    //data->ch, gst_event_type_get_name(eventType)); GstCaps *caps;
    // gst_event_parse_caps (event, &caps);
    // GstStructure *structure = gst_caps_get_structure (caps, 0);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] caps[%d] : %s", _FILE_, __LINE__,
    //data->ch, gst_caps_to_string(caps)); gst_structure_free(structure);
    // gst_caps_unref(caps);
    // if (!(res = gst_ffmpegmux_setcaps (pad, caps))) goto beach;
    break;
  }
  case GST_EVENT_SEGMENT: {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__,
    //data->ch, gst_event_type_get_name(eventType));
    break;
  }
  case GST_EVENT_STREAM_START: {
    data->start_f = 1;
    __LOG(LOG_INFO, "[GST][%s:%d] ch%d stream start", _FILE_, __LINE__,
          data->ch);
    break;
  }
  default: {
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch:%d Event Type: %s", _FILE_, __LINE__,
    //data->ch, gst_event_type_get_name(eventType));
    break;
  }
  }

  return GST_PAD_PROBE_OK;
}

static void split(gpointer data) {
  MuxSinkElement *be = (MuxSinkElement *)data;
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  g_signal_emit_by_name(be->sink, "split-now");
}

guint8 MuxSinkBin::getStartFlag() {
  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  return muxSinkData.start_f;
}

gboolean MuxSinkBin::splitNow(gpointer data, gboolean timer_en) {
  // MuxBin* muxBin = MuxBin::getInstance();
  // MuxBin* muxBin = (MuxBin *)data;

  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__,
  //muxSinkData.ch);

  // g_signal_emit_by_name (be.sink, "split-at-running-time");
  g_signal_emit_by_name(be.sink, "split-after");
  // g_signal_emit_by_name (be.sink, "split-now");

  if (timer_en) {
    __LOG(LOG_NOTICE, "[GST][%s:%d] split timer start ch : %d", _FILE_,
          __LINE__, muxSinkData.ch);
    g_timeout_add_seconds(cmdArg.duration * 60, (GSourceFunc)split, &be);
  }

  return TRUE;
}

void MuxSinkBin::setSplitMsec(gint msec) { muxSinkData.split_msec = msec; }

void MuxSinkBin::setLastRunningTime(GstClockTime rt) {
  muxSinkData.last_running_time = rt;
}

gint MuxSinkBin::getSplitMsec() { return muxSinkData.split_msec; }

GstClockTime MuxSinkBin::getSplitRunningTime() {
  return muxSinkData.split_running_time;
}

void MuxSinkBin::setSplitRunningTime(GstClockTime rt) {
  muxSinkData.split_running_time = rt;
  g_atomic_int_set(&muxSinkData.split_rt_stale,
                   GST_CLOCK_TIME_IS_VALID(rt) ? 0 : 1);
}

gboolean MuxSinkBin::isSplitRunningTimeFresh() {
  return g_atomic_int_get(&muxSinkData.split_rt_stale) == 0;
}

gchararray format_location(GstElement *sink, guint arg0, gpointer data) {
  MuxSinkData *info = (MuxSinkData *)data;
  GDateTime *datetime = g_date_time_new_now_local();
  gint sec = g_date_time_get_second(datetime);
  gint usec = g_date_time_get_microsecond(datetime);
  gchararray file_name;
  gchar *date_str_ptr = NULL;
  gchar *ts_str_ptr = NULL;
  
  /* 마커는 '대표 채널' 이 아니라 '세션' 을 기준으로 한 번만 기록한다.
   * 채널을 지명하면 그 채널이 죽었을 때 마커가 영구히 멎는데, disconnect_mask 는
   * 런타임 중 갱신되지 않아 재선정이 걸리지 않는다. 세션 기준이면 이번 분의
   * 마커를 아직 아무도 안 썼을 때 지금 파일을 여는 채널이 맡으므로,
   * 한 채널만 살아 있어도 마커가 계속 기록된다. (아래 marker_claim 참조) */

  info->split_msec = sec * 1000 + usec / 1000;

  /* 새 조각이 열리는 중이다. split_msec 은 여기서(스트리밍 스레드) 즉시 갱신되지만
   * split_running_time 은 fragment-opened 버스 메시지를 메인 루프가 처리할 때까지
   * '이전 조각' 값으로 남는다. 그 사이에 splitCheck(500ms 타이머, 메인 루프)가 끼면
   * 채널마다 다른 조각의 running-time 을 비교해 분 단위 가짜 스큐 -> 불필요한 강제 분할이
   * 발생한다. 짝이 맞을 때까지 stale 로 표시해 벽시계 기준으로 안전하게 되돌린다. */
  g_atomic_int_set(&info->split_rt_stale, 1);

  if (cmdArg.audio_en && info->split_msec >= cmdArg.split_audio_min_msec) {
    __LOG(LOG_NOTICE, "[GST][%s:%d] audio limit %d msec over..", _FILE_,
          __LINE__, cmdArg.split_audio_min_msec);
  }

  // [캐싱 최적화] 1초 내 중복 요청은 캐시된 문자열 재사용
  // Mutex 경합 최소화: 데이터 복사 후 즉시 Unlock
  gchar local_date_str[32];
  gchar local_ts_str[32];
  gint64 current_unix_sec = g_date_time_to_unix(datetime);
  gint64 split_target_epoch = getSplitTargetEpoch();
  gint64 naming_epoch_sec = current_unix_sec + 1;

  if (split_target_epoch > 0) {
    gint64 duration_sec = (cmdArg.duration > 0) ? ((gint64)cmdArg.duration * 60) : 60;
    gint64 delta_sec = split_target_epoch - current_unix_sec;

    naming_epoch_sec = split_target_epoch;
    if (delta_sec > (duration_sec / 2)) {
      naming_epoch_sec = split_target_epoch - duration_sec;
    }
  }
  
  g_mutex_lock(&timestamp_cache_mutex);
  if (naming_epoch_sec != last_cache_unix_sec) {
    GDateTime *round_time = g_date_time_new_from_unix_local(naming_epoch_sec);
    if (!round_time) {
      round_time = g_date_time_add_seconds(datetime, 1);
    }
    gchar *tmp_date = g_date_time_format(round_time, "%Y%m%d_%H%M00");
    gchar *tmp_ts = g_date_time_format(round_time, "%Y%m%d_%H%M");
    
    g_strlcpy(cached_date_str, tmp_date, sizeof(cached_date_str));
    g_strlcpy(cached_timestamp_str, tmp_ts, sizeof(cached_timestamp_str));
    last_cache_unix_sec = naming_epoch_sec;

    g_free(tmp_date);
    g_free(tmp_ts);
    g_date_time_unref(round_time);
  }
  /* 이번 세션의 마커를 아직 아무도 기록하지 않았으면 이 채널이 맡는다.
   * 같은 락 안에서 판정+선점하므로 채널이 동시에 진입해도 정확히 1회만 참이 된다. */
  gboolean marker_claim = (naming_epoch_sec != last_marked_epoch);
  if (marker_claim)
    last_marked_epoch = naming_epoch_sec;
  g_strlcpy(local_date_str, cached_date_str, sizeof(local_date_str));
  g_strlcpy(local_ts_str, cached_timestamp_str, sizeof(local_ts_str));
  g_mutex_unlock(&timestamp_cache_mutex);

  // [snprintf 최적화] 안전한 버퍼 경로 생성 (Mutex 밖에서 수행)
  char path_buf[1024];
  int written;
  const char *ext = (g_strcmp0(cmdArg.muxer, "ts") == 0) ? "ts" : "mp4";
  
  written = snprintf(path_buf, sizeof(path_buf), "%s/%s_%s-ch%d.%s.part", 
                     cmdArg.mntDir, cmdArg.ohtName, local_date_str, info->ch, ext);
  
  if (written >= (int)sizeof(path_buf)) {
    __LOG(LOG_ERR, "[GST][%s:%d] Path truncated! Using fallback", _FILE_, __LINE__);
    // Fallback: 안전한 짧은 이름 사용
    snprintf(path_buf, sizeof(path_buf), "/tmp/fallback_ch%d_%ld.mp4", info->ch, (long)current_unix_sec);
  }
  file_name = g_strdup(path_buf);

  // 타임스탬프 저장 (fragment-closed에서 사용)
  if (info->last_timestamp) g_free(info->last_timestamp);
  info->last_timestamp = g_strdup(local_ts_str);

  // 시작 시간 기록 (chk_cam_operate.sh용 - 세션당 1회, 먼저 여는 채널이 수행)
  if (marker_claim) {
    // 어느 채널이 세션 마커를 썼는지 바뀔 때만 남긴다 (매 분 찍으면 소음).
    static gint last_marker_ch = -1;
    if (info->ch != last_marker_ch) {
      __LOG(LOG_NOTICE, "[GST][%s:%d] session marker writer: ch%d -> ch%d",
            _FILE_, __LINE__, last_marker_ch, info->ch);
      last_marker_ch = info->ch;
    }
    // 파일명에 쓰인 세션(분)을 별도로 남긴다. 벽시계 분과 어긋날 수 있어
    // (경계 직전 호출 시 naming 이 다음 분으로 올림) 검사 측이 이 값으로
    // 대조해야 한다. 트리거인 start_video_time_chk 보다 먼저 기록한다.
    // 부분 기록이 읽히지 않도록 임시 파일에 쓴 뒤 rename 으로 원자적 게시한다.
    // 실패 시 임시 파일만 지워, 검사 측이 기존 방식으로 폴백하게 둔다.
    const char *sess_tmp = "/tmp/start_video_session_chk.tmp";
    int sfd = open(sess_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sfd >= 0) {
      size_t sess_len = strlen(local_ts_str);
      gboolean sess_ok = (write(sfd, local_ts_str, sess_len) == (ssize_t)sess_len);
      close(sfd);
      if (!sess_ok || rename(sess_tmp, "/tmp/start_video_session_chk") != 0)
        unlink(sess_tmp);
    }
    // [I/O 안정성] O_TRUNC 없이 열고 락 획득 후에만 truncate 수행
    int fd = open("/tmp/start_video_time_chk", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        if (ftruncate(fd, 0) == 0) {
          gchar *time_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
          write(fd, time_str, strlen(time_str));
          g_free(time_str);
        }
        flock(fd, LOCK_UN);
      }
      close(fd);
    }
  }

  g_date_time_unref(datetime);
  return file_name;
}

void MuxSinkBin::handle_last_sample() {
  GstSample *sample;
  g_object_get(be.sink, "last-sample", &sample, NULL);

  if (sample) {
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      // 마지막 샘플 데이터 처리
      g_print("Last sample size: %zu\n", map.size);
      gst_buffer_unmap(buffer, &map);
    }

    if (caps) {
      gchar *caps_str = gst_caps_to_string(caps);
      g_print("Last sample caps: %s\n", caps_str);
      g_free(caps_str);
    }

    gst_sample_unref(sample);
  } else {
    g_print("No last sample available.\n");
  }
}

MuxSinkBin *MuxSinkBin::getInstance() {
  static MuxSinkBin instance;
  return &instance;
}

MuxSinkBin::MuxSinkBin() {
  // 생성자 코드 추가
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  sinkAudioPad = NULL;
  sinkVideoPad = NULL;
  be.bin = NULL;
  muxSinkData.start_f = 0;
  muxSinkData.split_msec = 0;
  muxSinkData.split_running_time = GST_CLOCK_TIME_NONE;
  muxSinkData.split_rt_stale = 1;  /* 아직 조각이 열리지 않았다 */
  muxSinkData.last_timestamp = NULL;
  muxSinkData.last_running_time = GST_CLOCK_TIME_NONE;
  muxSinkData.last_end_time = GST_CLOCK_TIME_NONE;
  muxSinkData.last_duration = GST_CLOCK_TIME_NONE;
}

MuxSinkBin::~MuxSinkBin() {
  // 소멸자 코드 추가
  __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__,
        muxSinkData.ch);
  if (muxSinkData.last_timestamp) {
    g_free(muxSinkData.last_timestamp);
    muxSinkData.last_timestamp = NULL;
  }
}

gboolean MuxSinkBin::addBinVideoSinkPad() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
        muxSinkData.ch);

  GstPad *target_pad = gst_element_get_request_pad(be.sink, "video_aux_%u");
  sinkVideoPad = gst_ghost_pad_new(
      g_strdup_printf("video_pad_ch%d", muxSinkData.ch), target_pad);
  if (target_pad)
    gst_object_unref(target_pad);
  // sinkVideoPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d",
  // muxSinkData.ch), gst_element_get_request_pad(be.mux, "video_0"));
  // sinkAudioPad = gst_element_get_static_pad(be.mux,
  // g_strdup_printf("sinkvideopad%d", muxSinkData.ch));
  gst_pad_add_probe(sinkVideoPad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
                    (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

  return gst_element_add_pad(be.bin, sinkVideoPad);
}

gboolean MuxSinkBin::addBinAudioSinkPad() {
  __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
        muxSinkData.ch);

  GstPad *target_pad = gst_element_get_request_pad(be.sink, "audio_%u");
  sinkAudioPad = gst_ghost_pad_new(
      g_strdup_printf("audio_pad_ch%d", muxSinkData.ch), target_pad);
  if (target_pad)
    gst_object_unref(target_pad);
  // sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d",
  // muxSinkData.ch), gst_element_get_request_pad(be.mux, "audio_0"));
  // sinkAudioPad = gst_ghost_pad_new(g_strdup_printf("sinkaudiopad%d",
  // muxSinkData.ch), gst_element_get_static_pad(be.mux,
  // g_strdup_printf("sinkaudiostaticpad%d", muxSinkData.ch)));
  gst_pad_add_probe(sinkAudioPad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
                    (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

  return gst_element_add_pad(be.bin, sinkAudioPad);
}

GstPad *MuxSinkBin::getBinVideoSinkPad() {
  if (sinkVideoPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, muxSinkData.ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          muxSinkData.ch);

  return sinkVideoPad;
}

GstPad *MuxSinkBin::getBinAudioSinkPad() {
  if (sinkAudioPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, muxSinkData.ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          muxSinkData.ch);

  return sinkAudioPad;
}

GstPad *MuxSinkBin::getBinQueuePad() {
  if (sinkPad == NULL)
    __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__,
          __FUNCTION__, muxSinkData.ch);
  else
    __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__,
          muxSinkData.ch);

  return sinkPad;
}

gboolean MuxSinkBin::init(guint8 num) {
  gboolean ret = 0;
  muxSinkData.ch = num;
  muxSinkData.start_f = 0;

  if (be.bin)
    return 0;

  be.bin = gst_bin_new(g_strdup_printf("muxSinkBin%d", muxSinkData.ch));
  // me.sink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
  __LOG(LOG_INFO, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__,
        muxSinkData.ch);
  // g_object_set(me.sink, "max-size-time", 10*GST_SECOND, NULL);
  //  식별을 위해 엘리먼트 이름에 채널 포함
  be.sink = gst_element_factory_make(
      "splitmuxsink", g_strdup_printf("splitmuxsink_ch%d", muxSinkData.ch));
  be.mp4mux = gst_element_factory_make("mp4mux", "mp4mux");
  be.tsmux = gst_element_factory_make("mpegtsmux", "mpegtsmux");
  be.qtmux = gst_element_factory_make("qtmux", "qtmux");
  be.queue = gst_element_factory_make(QUEUE_TYPE, "queue");
  const gboolean use_h265 = g_strcmp0(cmdArg.enc, ENC_H265) == 0;
  const gchar *parser_factory = use_h265 ? "h265parse" : "h264parse";
  be.parse = gst_element_factory_make(parser_factory, parser_factory);
  be.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");

  if (!be.bin || !be.sink || !be.mp4mux || !be.tsmux || !be.qtmux ||
      !be.queue || !be.parse || !be.capsfilter) {
    __LOG(LOG_CRIT, "[GST][%s:%d] element create error", _FILE_, __LINE__);
    return ret;
  }
  gst_bin_add_many(GST_BIN(be.bin), be.parse, be.sink, be.capsfilter, NULL);
  ret = gst_bin_add(GST_BIN(pipeline), be.bin);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] bin add error in pipeline", _FILE_, __LINE__);
    return ret;
  }

#if 1
#define MUX_PROPERTIES "muxer"

  GstCaps *caps;
  g_object_set(be.sink, "async-finalize", FALSE, NULL);
  if (g_strcmp0(cmdArg.muxer, "ts") == 0) {
    g_object_set(be.sink, MUX_PROPERTIES, be.tsmux, NULL);
    caps = gst_caps_from_string(use_h265
                                    ? "video/x-h265,stream-format=byte-stream,alignment=au"
                                    : "video/x-h264,stream-format=byte-stream,alignment=au");
  } else if (g_strcmp0(cmdArg.muxer, "qt") == 0) {
    g_object_set(be.sink, MUX_PROPERTIES, be.qtmux, NULL);
    caps = gst_caps_from_string(use_h265
                                    ? "video/x-h265,stream-format=hvc1,alignment=au"
                                    : "video/x-h264,stream-format=avc,alignment=au");
  } else {
    g_object_set(be.sink, MUX_PROPERTIES, be.mp4mux, NULL);
    caps = gst_caps_from_string(use_h265
                                    ? "video/x-h265,stream-format=hvc1,alignment=au"
                                    : "video/x-h264,stream-format=avc,alignment=au");
  }
  g_object_set(be.capsfilter, "caps", caps, NULL);
  gst_caps_unref(caps);

  const char *mode = cmdArg.muxer;
  g_signal_connect(be.sink, "muxer-added", G_CALLBACK(muxer_added),
                   (gpointer)mode);

  ret = gst_element_link_many(be.parse, be.capsfilter, NULL);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] bin link error in pipeline", _FILE_,
          __LINE__);
    return ret;
  }

  GstPad *muxsrcpad = gst_element_get_static_pad(be.capsfilter, "src");

  // splitmuxsink가 제공하는 pad 템플릿 이름이 환경마다 다를 수 있어서
  // 보통은 아래 중 하나가 맞음: "video" 또는 "video_%u"
  GstPad *muxsinkpad = gst_element_get_request_pad(be.sink, "video_aux_%u");
  // 만약 NULL이면 "video_%u"로 재시도
  if (!muxsinkpad)
    muxsinkpad = gst_element_get_request_pad(be.sink, "video");

  if (!muxsinkpad) {
    __LOG(LOG_CRIT,
          "[GST] splitmuxsink has no request pad video/video_aux_%%u");
    gst_object_unref(muxsrcpad);
    return FALSE;
  }

  if (gst_pad_link(muxsrcpad, muxsinkpad) != GST_PAD_LINK_OK) {
    __LOG(LOG_CRIT, "[GST] pad link capsfilter->splitmuxsink(video) failed");
    gst_object_unref(muxsrcpad);
    gst_object_unref(muxsinkpad);
    return FALSE;
  }

  gst_object_unref(muxsrcpad);
  gst_object_unref(muxsinkpad);

  /* videorate 없을 때 PTS 없는 버퍼가 mp4mux에 도달하면 크래시.
   * capsfilter src (= splitmuxsink 직전)에서 드롭. */
  if (!cmdArg.videorate_en) {
    GstPad *cf_srcpad = gst_element_get_static_pad(be.capsfilter, "src");
    if (cf_srcpad) {
      gst_pad_add_probe(cf_srcpad, GST_PAD_PROBE_TYPE_BUFFER,
                        (GstPadProbeCallback)drop_no_pts_probe_mux,
                        &muxSinkData.ch, NULL);
      gst_object_unref(cf_srcpad);
    }
  }
#endif

  guint64 duration;
  // duration = ((615)*GST_SECOND*cmdArg.duration)/10;
  duration = GST_SECOND * cmdArg.duration * 60;
  __LOG(LOG_INFO, "[GST][%s:%d] file duration : %lld", _FILE_, __LINE__,
        duration);

  g_object_set(be.sink, "max-size-time", duration, NULL);
  // 키 프레임 요청 활성화 (분할 시점에 키 프레임 강제 요청)
  g_object_set(be.sink, "send-keyframe-requests", TRUE, NULL);
  // 버스 메시지로 fragment 상태 전달 (디버깅용)
  g_object_set(be.sink, "message-forward", TRUE, NULL);
  // g_object_set(be.sink, "reserved-max-duration", 3000000000000000000, NULL);
  // g_object_set(be.sink, "reserved-moov-update-period", 1000000000, NULL);
  // g_object_set(be.sink, "use-robust-muxing", TRUE, NULL);
  g_object_set(be.queue, "max-size-time", GST_SECOND / 2, "max-size-buffers",
               cmdArg.fps[STREAM_REC][muxSinkData.ch] / 2, "leaky",
               LEAKY_DOWNSTREAM, NULL);
  g_object_set(be.parse, "config-interval", 1, NULL);
#if 0
    GstStructure *muxer_properties = gst_structure_new("application/x-gst-mp4mux",
                                         "reserved-moov-update-period", G_TYPE_UINT64, 1000000000,
                                         "reserved-max-duration", G_TYPE_UINT64, 3000000000000000000, 
                                         "faststart", G_TYPE_BOOLEAN, TRUE,
                                         NULL);
#endif
#if 0
    GstStructure *muxer_properties = gst_structure_new_empty("muxer-properties");
    gst_structure_set(muxer_properties, "latency", G_TYPE_UINT64, 1000, "faststart", G_TYPE_BOOLEAN, TRUE, \
                "reserved-moov-update-period", G_TYPE_UINT64, 1000000000, "reserved-max-duration", G_TYPE_UINT64, 3000000000000000000, NULL);

    g_object_set(be.sink, "muxer-properties", muxer_properties, NULL);
    gst_structure_free(muxer_properties);
#endif

  // g_object_set(be.mp4mux, "latency", 100000, NULL);
  // g_object_set(be.mp4mux, "min-upstream-latency", 1000, NULL);
  // g_object_set(be.mp4mux, "fragment-duration", 1000, NULL);
#if 0
    g_object_set(be.mp4mux, "dts-method", 1, NULL);
    g_object_set(be.mp4mux, "emit-signals", 0, NULL);
    g_object_set(be.mp4mux, "faststart", 0, NULL);
    g_object_set(be.mp4mux, "faststart-file", "/tmp/qtmux-1870964074", NULL);
    g_object_set(be.mp4mux, "force-chunks", 0, NULL);
    g_object_set(be.mp4mux, "force-create-timecode-trak", 0, NULL);
    g_object_set(be.mp4mux, "fragment-duration", 0, NULL);
    g_object_set(be.mp4mux, "interleave-bytes", 0, NULL);
    g_object_set(be.mp4mux, "interleave-time", 250000000, NULL);
    g_object_set(be.mp4mux, "latency", 0, NULL);
    g_object_set(be.mp4mux, "max-raw-audio-drift", 40000000, NULL);
    g_object_set(be.mp4mux, "min-upstream-latency", 0, NULL);
    g_object_set(be.mp4mux, "moov-recovery-file", NULL, NULL);
    g_object_set(be.mp4mux, "movie-timescale", 0, NULL);
    g_object_set(be.mp4mux, "name", "mp4mux0", NULL);
    //g_object_set(be.mp4mux, "parent", 0, NULL);
    g_object_set(be.mp4mux, "presentation-time", 1, NULL);
    g_object_set(be.mp4mux, "reserved-bytes-per-sec", 550, NULL);
    g_object_set(be.mp4mux, "reserved-max-duration", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "reserved-moov-update-period", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "reserved-prefill", 0, NULL);
    g_object_set(be.mp4mux, "start-gap-threshold", 0, NULL);
    g_object_set(be.mp4mux, "start-time", 18446744073709551615, NULL);
    g_object_set(be.mp4mux, "start-time-selection", 0, NULL);
    g_object_set(be.mp4mux, "streamable", 0, NULL);
    g_object_set(be.mp4mux, "trak-timescale", 0, NULL);
#endif

  g_signal_connect(be.sink, "format-location", G_CALLBACK(format_location),
                   &muxSinkData);
  // fragment-closed 신호는 1.18.0 splitmuxsink에 존재하지 않으므로 버스
  // 메시지로 대체됨 g_signal_connect(be.sink, "fragment-closed",
  // G_CALLBACK(on_fragment_closed), &muxSinkData);
  g_signal_connect(be.sink, "sink-added", G_CALLBACK(sink_added), NULL);

  // if(!gst_element_add_pad(me.bin,
  // gst_ghost_pad_new(g_strdup_printf("muxBinSink_audio_ch%d", ch),
  // gst_element_get_request_pad(me.sink, "audio_%u")))) g_error("error");

  // if(!gst_element_add_pad(me.bin,
  // gst_ghost_pad_new(g_strdup_printf("muxBinSink_video_ch%d", ch),
  // gst_element_get_request_pad(me.sink, "video_aux_%u")))) g_error("error");

  sinkPad =
      gst_ghost_pad_new("srcpad", gst_element_get_static_pad(be.parse, "sink"));
  gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
                    (GstPadProbeCallback)handle_eos_event, &muxSinkData, NULL);

  ret = gst_element_add_pad(be.bin, sinkPad);
  if (!ret) {
    __LOG(LOG_CRIT, "[GST][%s:%d] record bin add err", _FILE_, __LINE__);
    return ret;
  }

  return ret;
}
