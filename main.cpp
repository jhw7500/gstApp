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
#include "ipc.h"
#include "muxSinkBin.h"
#include "parser.h"
#include "recordBin.h"
#include "rtspServerBin.h"
#include "tcpServer.h"
#include "testBin.h"
#include "util.h"
#include "videoBin.h"
#include <fcntl.h>
#include <unistd.h>
// #include <signal.h>

#define APP_VERSION "1.3"
#define MAX_SNAPBACK_DRIFT_MS 30000
#define MIN_SPLIT_INTERVAL_SEC 5
#define SNAP_BACK_GRACE_PERIOD_MS 58000

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
} FragmentClosedEvent;

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
      g_free(ev->location);
      g_free(ev);
      break;
    }

    msBin[ev->ch].handleFragmentClosed(ev->location, ev->running_time, ev->duration);

    g_free(ev->location);
    g_free(ev);
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
// MuxSinkBin muxSinkBin[MAX_CHANNEL];
gboolean config_camera(gpointer user_data);

void handle_sigint(int sig) {
  // g_print("Caught signal %d, sending EOS to pipeline\n", sig);
  __LOG(LOG_EMERG, "[GST][%s:%d] Caught signal %d, sending EOS to pipeline",
        _FILE_, __LINE__, sig);
  gst_element_send_event(pipeline, gst_event_new_eos());
}

gboolean bus_message_parse(GstBus *bus, GstMessage *message, gpointer data) {
  // PipeMain *info = (PipeMain *)data;
  gchar *str = NULL;
  static guint8 cam_cnt = 0;
  static GstState state = GST_STATE_VOID_PENDING;
  GstMessageType mType = GST_MESSAGE_TYPE(message);

  if (mType == GST_MESSAGE_QOS)
    return TRUE;

  if (mType == GST_MESSAGE_TAG)
    return TRUE;

  // if(mType == GST_MESSAGE_ELEMENT) return TRUE;

  if (mType == GST_MESSAGE_STREAM_STATUS)
    return TRUE;
  // printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
  // if(mType == GST_MESSAGE_STATE_CHANGED) return TRUE;

  //__LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__,
  // GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));

  switch (mType) {
  case GST_MESSAGE_STATE_CHANGED: {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(message, &old_state, &new_state,
                                    &pending_state);
    if (state != old_state) {
      //__LOG(LOG_NOTICE, "[GST][%s:%d] from %s to %s in %s", _FILE_, __LINE__,
      // gst_element_state_get_name(old_state),
      // gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
      // g_print("from %s to %s at %s\n", gst_element_state_get_name(old_state),
      // gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
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
      // Use safe_write_file instead of system("echo ...")
      if (safe_write_file("/tmp/gst_err", err->message) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] err writing to /tmp/gst_err", _FILE_,
              __LINE__);
      g_error_free(err);
    }
    if (debug) {
      __LOG(LOG_ERR, "[GST][%s:%d] error debug : %s\n", __FILE__, __LINE__,
            (debug) ? debug : "none");
      // Use safe_write_file instead of system("echo ...")
      if (safe_write_file("/tmp/gst_err", debug) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] err writing to /tmp/gst_err", _FILE_,
              __LINE__);
      g_free(debug);
    }
    // destroy();
    gst_element_send_event(pipeline, gst_event_new_eos());
    break;
  }

  case GST_MESSAGE_EOS: {
    // printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
    __LOG(LOG_EMERG, "[GST][%s:%d] GST_MESSAGE_EOS", _FILE_, __LINE__);
    if (is_interrupted) {
      cam_cnt++;
      g_main_loop_quit(loop);
      // if(cam_cnt >= MAX_PIPELINE) destroy();
    } else {
      is_interrupted = TRUE;
      g_main_loop_quit(loop);
      // gst_element_set_state(pipeline, GST_STATE_READY);
      // gst_element_set_state(pipeline, GST_STATE_PLAYING);
      // gst_element_get_state(pipeline[info->index], NULL, NULL,
      // GST_CLOCK_TIME_NONE); gst_element_seek(pipeline[info->index], 1.0,
      // GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0,
      // GST_SEEK_TYPE_SET, -1); gst_element_set_state(pipeline[info->index],
      // GST_STATE_NULL); change_file_datetime(NULL);
    }

    break;
  }

  case GST_MESSAGE_ELEMENT: {
    //__LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__,
    // gst_structure_to_string(gst_message_get_structure(message)));
    // g_print("%s\n",
    // gst_structure_to_string(gst_message_get_structure(message)));
    const GstStructure *structure = gst_message_get_structure(message);
    if (gst_structure_has_name(structure, "splitmuxsink-fragment-opened")) {
      const gchar *location =
          g_value_get_string(gst_structure_get_value(structure, "location"));
      GstClockTime running_time = GST_CLOCK_TIME_NONE;
      const GValue *rv = gst_structure_get_value(structure, "running-time");
      if (rv)
        running_time = g_value_get_uint64(rv);

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

      // 어떤 채널의 splitmuxsink인지 찾아서 핸들러 호출
      ThreadArgs *tArgs = (ThreadArgs *)data;
      MuxSinkBin *msBin = (MuxSinkBin *)tArgs->arg3;

      GstElement *src_element = (GstElement *)GST_MESSAGE_SRC(message);
      for (int i = 0; i < MAX_CHANNEL; i++) {
        if (msBin[i].be.sink == src_element) {
          if (fragment_closed_queue) {
            FragmentClosedEvent *ev = g_new0(FragmentClosedEvent, 1);
            ev->ch = i;
            ev->location = g_strdup(location);
            ev->running_time = running_time;
            ev->duration = duration;
            g_async_queue_push(fragment_closed_queue, ev);
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

  case GST_MESSAGE_STREAM_STATUS: {
    GstStreamStatusType type;
    GstElement *owner;
    const GValue *val;
    gchar *path;
    GstTask *task = NULL;

    g_message("received STREAM_STATUS");
    gst_message_parse_stream_status(message, &type, &owner);

    val = gst_message_get_stream_status_object(message);

    g_message("type:   %d", type);
    path = gst_object_get_path_string(GST_MESSAGE_SRC(message));
    g_message("source: %s", path);
    g_free(path);
    path = gst_object_get_path_string(GST_OBJECT(owner));
    g_message("owner:  %s", path);
    g_free(path);
    g_message("object: type %s, value %p", G_VALUE_TYPE_NAME(val),
              g_value_get_object(val));

    /* see if we know how to deal with this object */
    if (G_VALUE_TYPE(val) == GST_TYPE_TASK) {
      task = (GstTask *)g_value_get_object(val);
    }

    switch (type) {
    case GST_STREAM_STATUS_TYPE_CREATE:
      g_message("created task %p", task);
      break;
    case GST_STREAM_STATUS_TYPE_ENTER:
      /* g_message ("raising task priority"); */
      /* setpriority (PRIO_PROCESS, 0, -10); */
      break;
    case GST_STREAM_STATUS_TYPE_LEAVE:
      break;
    default:
      break;
    }
    break;
  }

  case GST_MESSAGE_QOS: {
#if 1
    gboolean live;
    guint64 running_time, stream_time, timestamp, duration;
    gst_message_parse_qos(message, &live, &running_time, &stream_time,
                          &timestamp, &duration);
    g_warning("GOt a QOS event %lu %lu %lu %lu", running_time, stream_time,
              timestamp, duration);
    gint64 jitter;
    gdouble prop;
    gint qual;
    gst_message_parse_qos_values(message, &jitter, &prop, &qual);
    g_warning("gotQoSE %lu %f %d", jitter, prop, qual);

    GstFormat format;
    guint64 processed;
    guint64 dropped;
    gst_message_parse_qos_stats(message, &format, &processed, &dropped);

    g_print("QoS Message:\n");
    g_print("Format: %s\n", gst_format_get_name(format));
    g_print("Processed: %lu\n", processed);
    g_print("Dropped: %lu\n", dropped);
#endif
    break;
  }

  case GST_MESSAGE_TAG: {
#if 1
    GstTagList *tags = NULL;
    gst_message_parse_tag(message, &tags);
    g_print("Got tags from element %s\n", GST_OBJECT_NAME(message->src));
    gst_tag_list_foreach(tags, print_tag, NULL);
    gst_tag_list_free(tags);
    // handle_tags (tags);
    // gst_tag_list_unref (tags);
#endif
    break;
  }
  case GST_MESSAGE_CLOCK_PROVIDE: {
    g_print("Clock provide event from element %s\n",
            GST_OBJECT_NAME(message->src));
    break;
  }

  case GST_MESSAGE_CLOCK_LOST: {
    /* Get a new clock */
    // g_print ("GST_MESSAGE_CLOCK_LOST\r");
    g_print("Clock lost event from element %s\n",
            GST_OBJECT_NAME(message->src));
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    break;
  }

  case GST_MESSAGE_LATENCY: {
    // when pipeline latency is changed, this msg is posted on the bus. we then
    // have to explicitly tell the pipeline to recalculate its latency
    // FIXME: this never works!
#if 1
    if (!gst_bin_recalculate_latency(GST_BIN(pipeline)))
      g_print("Could not reconfigure latency.\n");
    else
      g_print("Reconfigured latency.\n");
    break;
#endif
  }

  case GST_MESSAGE_APPLICATION: {
    const GstStructure *s;
    s = gst_message_get_structure(message);
    if (gst_structure_has_name(s, "GstLaunchInterrupt")) {
      /* this application message is posted when we caught an interrupt and
       * we need to stop the pipeline. */
      g_print(("Interrupt: Stopping pipeline ...\n"));
      // res = ELR_INTERRUPT;
      // goto exit;
    }
    break;
  }

  case GST_MESSAGE_NEW_CLOCK: {
    if (cmdArg.stream_en[STREAM_REC] || cmdArg.audio_en) {
      // Use safe_write_file instead of system("echo ...")
      GDateTime *datetime = g_date_time_new_now_local();
      gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
      if (safe_write_file(DEFAULT_START_VIDEO_TIME_PATH, date_str) < 0)
        __LOG(LOG_ERR, "[GST][%s:%d] Failed to write start time to %s in %s",
              _FILE_, __LINE__, DEFAULT_START_VIDEO_TIME_PATH, __FUNCTION__);
      else
        __LOG(LOG_NOTICE, "[GST][%s:%d] Wrote start time to %s in %s", _FILE_,
              __LINE__, DEFAULT_START_VIDEO_TIME_PATH, __FUNCTION__);
      g_free(date_str);
      g_date_time_unref(datetime);
    }
    break;
  }

  case GST_MESSAGE_ASYNC_DONE: {
    __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__,
          GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME(message->src));
    break;
  }

  case GST_MESSAGE_WARNING: {
    GError *gerror;
    gchar *debug;
    gst_message_parse_warning(message, &gerror, &debug);
    gst_object_default_error(GST_MESSAGE_SRC(message), gerror, debug);
    g_error_free(gerror);
    g_free(debug);
    break;
  }

  case GST_MESSAGE_STREAM_START: {
    __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__,
          GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME(message->src));
    break;
  }

  default:
    __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__,
          GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME(message->src));
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
  static gint target_min;
  static gint64 last_split_ts = 0; // 마지막 분할 시점 (단조 클럭)
  static gint last_split_min = -1; // 마지막 분할 시점의 '분' 정보
  guint8 i;
  GDateTime *datetime = g_date_time_new_now_local();
  gint sec = g_date_time_get_second(datetime);
  gint min = g_date_time_get_minute(datetime);

  if (start_flag == 0) {
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      if (muxSinkBin[i].getStartFlag() == 0) {
        g_date_time_unref(datetime);
        return;
      }
    }
    target_min = (sec != startSec) ? g_date_time_get_minute(g_date_time_add_minutes(datetime, 1)) : min;
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

  if (diff >= 0) {
    gboolean any_misaligned = FALSE;
    gint splitMax = 0, splitMin = 59999;
    gint active_count = 0;

    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      gint sm = muxSinkBin[i].getSplitMsec();
      active_count++;
      if (diff < 5) {
        __LOG(LOG_DEBUG, "[GST][%s:%d] ch%d sm:%dms, diff:%ds", _FILE_, __LINE__, i, sm, diff);
      }
      if (sm >= cmdArg.split_max_msec) any_misaligned = TRUE;
      if (sm > splitMax) splitMax = sm;
      if (sm < splitMin) splitMin = sm;
    }

    gboolean is_fully_aligned = (!any_misaligned);
    if (active_count > 1 && (splitMax - splitMin >= cmdArg.split_diff_msec)) {
      is_fully_aligned = FALSE;
    }

    if (is_fully_aligned) {
      target_min = (target_min + cmdArg.duration) % 60;
      __LOG(LOG_NOTICE, "[GST][%s:%d] All channels aligned to 00s. Next target : %02dm %02ds", 
            _FILE_, __LINE__, target_min, startSec);
      last_split_ts = g_get_monotonic_time();
      last_split_min = min; 
      g_date_time_unref(datetime);
      return;
    }

    gboolean do_force = TRUE;
    for (i = 0; i < MAX_CHANNEL; i++) {
      if (!cmdArg.cam[i].enable) continue;
      gint sm = muxSinkBin[i].getSplitMsec();
      if (sm >= SNAP_BACK_GRACE_PERIOD_MS && (diff * 1000 < cmdArg.split_max_msec)) {
        do_force = FALSE;
        break;
      }
    }

    if (do_force) {
      gint64 now_ts = g_get_monotonic_time();
      if (min == last_split_min && (now_ts - last_split_ts) < (MIN_SPLIT_INTERVAL_SEC * 1000000)) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Skip forced split (Same minute interval protection: %ldms)", 
              _FILE_, __LINE__, (long)((now_ts - last_split_ts) / 1000));
        do_force = FALSE;
      }
    }

    if (do_force) {
      __LOG(LOG_ERR, "[GST][%s:%d] Snap-back split (diff:%ds, smMax:%dms, skew:%dms)", 
            _FILE_, __LINE__, diff, splitMax, (active_count > 1 ? splitMax - splitMin : 0));
      
      // [P1 해결] dual_enc=FALSE일 때만 encoder 객체(eBin)에 접근
      if (cmdArg.dual_enc == FALSE && eBin != NULL) {
        for (i = 0; i < MAX_CHANNEL; i++) {
          if (cmdArg.cam[i].enable) {
            eBin[i].forceKeyframe();
          }
        }
      }

      gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
      safe_write_file(DEFAULT_START_VIDEO_TIME_PATH, date_str);
      g_free(date_str);

      for (i = 0; i < MAX_CHANNEL; i++) {
        if (cmdArg.cam[i].enable) {
          muxSinkBin[i].splitNow(NULL, FALSE);
          muxSinkBin[i].setSplitMsec(DEFAULT_SPLIT_MAX_MSEC); 
        }
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
      if (val[ch_num0] == 0)
        __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display",
              _FILE_, __LINE__, ch_num0, ch_num1);

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
      if (val[ch_num1] == 0)
        __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display",
              _FILE_, __LINE__, ch_num1, ch_num0);

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

  if (parser->arg_parser(&argc, &argv) < 0)
    return -1;

  getPasswdWithAES(&parser->arg);
  // if(!strcmp(parser->arg.rtsp_passwd, DEFAULT_RTSP_PASSWD) == 0)

  __LOG(LOG_INFO,
        "[GST][%s:%d] %s version : %s linked against Gstreamer %d.%d.%d %s",
        __FILE__, __LINE__, parser->arg.appname, APP_VERSION, major, minor,
        micro, nano_str);

  cmdArg = parser->arg;
  if (parser->check_arg() < 0)
    return -1;

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

  for (i = 0; i < MAX_CHANNEL; i++) {
    // if(!(cmdArg.ch_enable & (0x1 << i))) continue;
    if (!cmdArg.cam[i].enable)
      continue;
    // chNum = i;
    csiNum = (i / 2);
    __LOG(LOG_INFO, "[GST][%s:%d] ch[%d] enable", _FILE_, __LINE__, i);
    if (!videoBin[csiNum].init(csiNum)) {
      __LOG(LOG_CRIT, "[GST][%s:%d] csi%d video bin init err", _FILE_, __LINE__,
            csiNum);
      // goto main_end;
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
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d init err in encoderBin", _FILE_,
              __LINE__, i, csiNum);
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
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink init err", _FILE_,
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
      if (!rtspServerStart()) {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_,
              __LINE__);
        goto main_end;
      }

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

    if (cmdArg.audio_en) {

      audioBin.init();

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
                       muxSinkBin[i].getBinAudioSinkPad()) != GST_PAD_LINK_OK) {
        __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio pad link err", _FILE_,
              __LINE__, i);
        // return -1;
        goto main_end;
      } else
        __LOG(LOG_INFO, "[GST][%s:%d] ch%d audio pad link", _FILE_, __LINE__,
              i);
#if 1
      if (rtspServerBin[4].audioInit()) {
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
#endif
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
  __LOG(LOG_INFO, "[GST][%s:%d] Main loop exit", _FILE_, __LINE__);

  if (gst_element_set_state(pipeline, GST_STATE_NULL) !=
      GST_STATE_CHANGE_SUCCESS) {
    for (i = 0; i < MAX_CHANNEL; i++) {
      // if(muxSinkBin[i].getBinVideoSinkPad())
      // muxSinkBin[i].handle_last_sample();
      if (muxSinkBin[i].getBinAudioSinkPad())
        gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(),
                           gst_event_new_eos());
      if (muxSinkBin[i].getBinVideoSinkPad()) {
        gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(),
                           gst_event_new_eos());
        gst_element_send_event(muxSinkBin[i].be.bin, gst_event_new_eos());
      }
      if (rtspServerBin[i].getBinSinkPad()) {
        gst_pad_send_event(rtspServerBin[i].getBinSinkPad(),
                           gst_event_new_eos());
        gst_element_send_event(rtspServerBin[i].re.bin, gst_event_new_eos());
      }
      if (captureBin[i].getBinSinkPad()) {
        gst_pad_send_event(captureBin[i].getBinSinkPad(), gst_event_new_eos());
        gst_element_send_event(captureBin[i].be.bin, gst_event_new_eos());
      }

      if (videoBin[i / 2].getBinRtspSrcPad(i))
        gst_pad_send_event(videoBin[i / 2].getBinRtspSrcPad(i),
                           gst_event_new_eos());
      if (videoBin[i / 2].getBinRecordSrcPad(i))
        gst_pad_send_event(videoBin[i / 2].getBinRecordSrcPad(i),
                           gst_event_new_eos());
      if (videoBin[i / 2].getBinCaptureSrcPad(i))
        gst_pad_send_event(videoBin[i / 2].getBinCaptureSrcPad(i),
                           gst_event_new_eos());
      // if(captureBin[i].add_cap_f == TRUE)
      // gst_pad_send_event(captureBin[i].getBinSinkPad(), gst_event_new_eos());
    }
  }

  gst_element_send_event(pipeline, gst_event_new_eos());

  // sleep(1);

main_end:
  __LOG(LOG_NOTICE, "[GST][%s:%d] main loop end", _FILE_, __LINE__);

  stop_fragment_closed_worker();

  if (threadArgs)
    g_free(threadArgs);

  if (terminalThread) {
    g_thread_join(terminalThread);
    g_thread_unref(terminalThread);
  }
  if (srtTimer_id) {
    g_source_remove(srtTimer_id);
  }

  rtspServerStop();

  __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state set NULL...", _FILE_,
        __LINE__);

  if (gst_element_set_state(pipeline, GST_STATE_NULL) ==
      GST_STATE_CHANGE_FAILURE) {
    __LOG(LOG_CRIT, "[GST][%s:%d] Failed to unset pipeline state", _FILE_,
          __LINE__);
  } else {
    __LOG(LOG_NOTICE, "[GST][%s:%d] Pipeline unset successfully", _FILE_,
          __LINE__);
  }

  if (pipeline)
    gst_object_unref(pipeline);

  if (cmdArg.tcp_en) {
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

  //__LOG(LOG_NOTICE, "[GST][%s:%d] exit", _FILE_, __LINE__);
  exit(0);
}
