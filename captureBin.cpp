/*
 * 
 * Cantops captureBin.cpp support
 * 
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 * 
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 * 
 * Description:
 */

#include "captureBin.h"
#include "parser.h"
#include "tcpServer.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/md5.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static gboolean init_compressor(CaptureData *info) {
    if (!info->tjCompressor) {
        __LOG(LOG_NOTICE, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
        info->tjCompressor = tjInitCompress();
        if (!info->tjCompressor) {
            __LOG(LOG_ERR, "[%s][%s:%d] Failed to initialize TurboJPEG: %s", CAP_LOG_KEY, _FILE_, __LINE__, tjGetErrorStr());
            return FALSE;
        }
    }
    return TRUE;
}

static void destroy_compressor(CaptureData *info) {
    if (info->tjCompressor) {
        __LOG(LOG_NOTICE, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
        tjDestroy(info->tjCompressor);
        info->tjCompressor = NULL;
    }
}

unsigned char *compress_frame_to_jpeg(GstSample *sample, long *jpeg_size, CaptureData *info) {
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *s = gst_caps_get_structure(caps, 0);

    int width, height;
    const gchar *format;

    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);
    format = gst_structure_get_string(s, "format");
    //__LOG(LOG_NOTICE, "[%s][%s:%d] format: %s, width: %d, height: %d", CAP_LOG_KEY, _FILE_, __LINE__, format, width, height);
    if (g_strcmp0(format, "RGBx") != 0) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Only RGB format supported here. Current format: %s", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, format);
        return NULL;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to map buffer", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        return NULL;
    }

    if (!init_compressor(info)) {
        gst_buffer_unmap(buffer, &map);
        return NULL;
    }

    unsigned char *jpeg_buf = NULL;
    int pixel_format = TJPF_RGBX;
    int jpeg_quality = info->quality; // 1~100
    int subsamp = TJSAMP_444; // No chroma subsampling

    //__LOG(LOG_NOTICE, "[%s][%s:%d] tjCompress2", CAP_LOG_KEY, _FILE_, __LINE__);
    int ret = tjCompress2(
        info->tjCompressor,
        map.data, width, width*4, height, pixel_format,
        &jpeg_buf, (unsigned long *)jpeg_size,
        subsamp, jpeg_quality,
        TJFLAG_FASTDCT
    );
    //__LOG(LOG_NOTICE, "[%s][%s:%d] gst_buffer_unmap", CAP_LOG_KEY, _FILE_, __LINE__);
    gst_buffer_unmap(buffer, &map);

    if (ret != 0) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d JPEG compression failed: %s", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, tjGetErrorStr());
        if (jpeg_buf) tjFree(jpeg_buf);
        return NULL;
    }

    return jpeg_buf;
}

// Async capture task structure
struct _AsyncCaptureTask {
    GstSample *sample;
    CaptureData *info;
    gchar *file_path;
    gint capture_index;

    // Timing from sample
    GstClockTime pts;
    GstClockTime running_time;

    // Linked Request
    std::shared_ptr<CaptureRequest> request;
};

// Forward declarations for buffered capture helpers
static void clear_buffering_buffers(CaptureData *info);
static gpointer buffering_feeder_thread(gpointer user_data);

static GstClockTime get_pipeline_running_time()
{
    if (!pipeline) return GST_CLOCK_TIME_NONE;
    GstClock *clock = gst_element_get_clock(pipeline);
    if (!clock) return GST_CLOCK_TIME_NONE;

    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);

    GstClockTime base = gst_element_get_base_time(pipeline);
    if (now <= base) return 0;
    return now - base;
}

// Callback data for idle handler
typedef struct {
    CaptureCompleteCallback callback;
    guint8 ch;
    gint completed_count;
    gpointer user_data;
} CallbackIdleData;

// Idle callback to invoke completion callback in main thread
static gboolean invoke_complete_callback_idle(gpointer user_data)
{
    CallbackIdleData *data = (CallbackIdleData *)user_data;
    if(data && data->callback) {
        data->callback(data->ch, data->completed_count, data->user_data);
    }
    g_free(data);
    return G_SOURCE_REMOVE;  // One-time callback
}

// Global timeout callback wrapper
static gboolean timeout_check_func(gpointer user_data)
{
    CaptureBin *bin = (CaptureBin *)user_data;
    return bin->checkTimeout();
}

// Worker thread for async file I/O
static gpointer capture_worker_thread(gpointer user_data)
{
    CaptureData *info = (CaptureData *)user_data;

    __LOG(LOG_INFO, "[%s][%s:%d] ch%d worker thread started", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);

    while(info->worker_running.load() || g_async_queue_length(info->task_queue) > 0)
    {
        // Wait for task with 100ms timeout
        AsyncCaptureTask *task = (AsyncCaptureTask *)g_async_queue_timeout_pop(info->task_queue, 100000);
        if(!task) continue;

        if (task->request && task->request->timed_out.load()) {
            if (task->sample) gst_sample_unref(task->sample);
            if (task->file_path) g_free(task->file_path);
            g_free(task);
            continue;
        }

        GstBuffer *buffer = NULL;
        FILE *fp = NULL;
        GstMapInfo map;
        gboolean success = FALSE;
        gsize written_bytes = 0;

        do {
            if(task->info->enc_type == CAP_ENC_TURBO)
            {
                long jpeg_size = 0;
                unsigned char *jpeg_buf = compress_frame_to_jpeg(task->sample, &jpeg_size, task->info);
                if(jpeg_buf && jpeg_size > 0) {
                    fp = fopen(task->file_path, "wb");
                    if(fp) {
                        written_bytes = fwrite(jpeg_buf, 1, jpeg_size, fp);
                        fclose(fp);
                        success = (written_bytes == (gsize)jpeg_size);
                        if (success) {
                            __LOG(LOG_INFO, "[%s][%s:%d] ch%d Saved JPEG to %s (%ld bytes)", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path, jpeg_size);
                        } else {
                            __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to write complete file %s (%zu/%ld bytes written)",
                                  CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path, (size_t)written_bytes, jpeg_size);
                        }
                    }
                    else {
                        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to open file %s for writing", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path);
                    }
                    tjFree(jpeg_buf);
                }
                else {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed jpeg_size : %ld", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, jpeg_size);
                }
            }
            else
            {
                buffer = gst_sample_get_buffer(task->sample);
                if(!buffer) {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch);
                    break;
                }

                if(!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to map buffer", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch);
                    break;
                }

                fp = fopen(task->file_path, "wb");
                if(fp) {
                    written_bytes = fwrite(map.data, 1, map.size, fp);
                    fclose(fp);
                    success = (written_bytes == map.size);
                    if (!success) {
                        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to write complete file %s (%zu/%zu bytes written)",
                              CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path, (size_t)written_bytes, (size_t)map.size);
                    }
                }
                else {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to open file %s for writing", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path);
                }
                gst_buffer_unmap(buffer, &map);
            }
        } while(0);

        // Per-file completion logging (keep it lightweight):
        // - Always log the first file of the request
        // - Log the last file when we know maxCnt
        if (success && task->request) {
            const gint last_index = task->request->maxCnt - 1;
            if (task->capture_index == 0 || (last_index >= 0 && task->capture_index == last_index)) {
                __LOG(LOG_INFO, "[%s][%s:%d] ch%d Saved frame idx=%d/%d: %s (%zu bytes)",
                      CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch,
                      task->capture_index, task->request->maxCnt, task->file_path, (size_t)written_bytes);
            }

            if (task->info->debug) {
                GstClockTime pts = task->pts;
                GstClockTime rt = task->running_time;
                GstClockTime delta = GST_CLOCK_TIME_NONE;
                GstClockTime latency = GST_CLOCK_TIME_NONE;

                if (GST_CLOCK_TIME_IS_VALID(pts) && GST_CLOCK_TIME_IS_VALID(task->info->last_saved_pts)) {
                    if (pts >= task->info->last_saved_pts) {
                        delta = pts - task->info->last_saved_pts;
                    }
                }
                if (GST_CLOCK_TIME_IS_VALID(rt) && GST_CLOCK_TIME_IS_VALID(pts)) {
                    if (rt >= pts) {
                        latency = rt - pts;
                    } else {
                        latency = 0;
                    }
                }

                __LOG(LOG_NOTICE,
                      "[%s][%s:%d] ch%d CAP sample: idx=%d/%d pts=%" GST_TIME_FORMAT " delta=%" GST_TIME_FORMAT " pipe=%" GST_TIME_FORMAT,
                      CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch,
                      task->capture_index, task->request->maxCnt,
                      GST_TIME_ARGS(pts), GST_TIME_ARGS(delta), GST_TIME_ARGS(latency));

                if (GST_CLOCK_TIME_IS_VALID(pts)) {
                    task->info->last_saved_pts = pts;
                }
            }
        }

        // Increment counter only after successful write (atomic operation)
        if(success && task->request) {
            // Increment WRITTEN count in request
            gint new_count = g_atomic_int_add(&task->request->captureCnt, 1) + 1;

            // Check if all captures for this request are complete
            if(new_count >= task->request->maxCnt) {
                gint64 elapsed_ms = (g_get_monotonic_time() - task->request->startTime) / 1000;
                // Atomic compare-and-swap to prevent double callback (timeout vs completion race)
                bool expected = false;
                if (task->request->responseSent.compare_exchange_strong(expected, true) &&
                    task->info->complete_callback) {
                    CallbackIdleData *cb_data = g_new0(CallbackIdleData, 1);
                    cb_data->callback = task->info->complete_callback;
                    cb_data->ch = task->info->ch;
                    cb_data->completed_count = new_count;
                    cb_data->user_data = task->request->userData; // Pass request context

                    g_idle_add(invoke_complete_callback_idle, cb_data);
                    __LOG(LOG_INFO, "[%s][%s:%d] ch%d capture complete callback triggered (%d/%d)",
                          CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, new_count, task->request->maxCnt);
                }

                __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d Capture request DONE: %s (written %d/%d, dropped %d, elapsed %ld ms)",
                      CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch,
                      task->request->filePath ? task->request->filePath : "(null)",
                      new_count, task->request->maxCnt, (gint)task->request->droppedCnt.load(), elapsed_ms);

                // Start buffered collection for next pending request (PNG only)
                {
                    std::lock_guard<std::mutex> lock(*task->info->queue_mutex);
                    if (task->info->enc_type == CAP_ENC_PNG &&
                        !task->info->buffering_active &&
                        !task->info->current_request &&
                        task->info->request_queue &&
                        !task->info->request_queue->empty()) {
                        std::shared_ptr<CaptureRequest> next = task->info->request_queue->front();
                        clear_buffering_buffers(task->info);
                        task->info->buffering_active = TRUE;
                        task->info->buffering_req = next;
                        task->info->buffering_first_pts = GST_CLOCK_TIME_NONE;
                        task->info->buffering_next_pts = GST_CLOCK_TIME_NONE;
                        task->info->buffering_period = (task->info->fps > 0) ? (GST_SECOND / task->info->fps) : (GST_SECOND / 1);
                        if (task->info->valve) {
                            g_object_set(task->info->valve, "drop", FALSE, NULL);
                        }
                        __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d buffered capture enabled for next request: fps=%d cnt=%d",
                              CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->info->fps, next->maxCnt);
                    }
                }

                // Request object managed by shared_ptr, will be deleted when last reference gone
            }
        }

        // Cleanup
        if(task->sample) gst_sample_unref(task->sample);
        if(task->file_path) g_free(task->file_path);
        g_free(task);
    }

    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d worker thread stopped", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
    return NULL;
}

static void clear_buffering_buffers(CaptureData *info)
{
    if (!info) return;
    for (auto *b : info->buffering_buffers) {
        if (b) gst_buffer_unref(b);
    }
    info->buffering_buffers.clear();
}

static gpointer buffering_feeder_thread(gpointer user_data)
{
    CaptureData *info = (CaptureData *)user_data;
    if (!info) return NULL;

    while (info->buffering_feeder_running.load()) {
        GstBuffer *buf = NULL;
        {
            std::lock_guard<std::mutex> lock(*info->queue_mutex);
            if (info->buffering_buffers.empty()) {
                break;
            }
            buf = info->buffering_buffers.front();
            info->buffering_buffers.pop_front();
        }

        GstFlowReturn ret;
        g_signal_emit_by_name(info->appsrc, "push-buffer", buf, &ret);
        if (ret != GST_FLOW_OK) {
            __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffered push-buffer failed: %d", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, ret);
            // On failure, appsrc may not have taken ownership.
            gst_buffer_unref(buf);
            break;
        }
    }

    // Cleanup any remaining buffers (if we bailed early)
    {
        std::lock_guard<std::mutex> lock(*info->queue_mutex);
        clear_buffering_buffers(info);
        info->buffering_req = nullptr;
    }

    info->buffering_feeder_running.store(false);
    return NULL;
}

static inline void tiny_sleep_ns(long ns) {
    struct timespec ts; ts.tv_sec = ns / 1000000000L; ts.tv_nsec = ns % 1000000000L;
    nanosleep(&ts, NULL);
}

static bool is_safe_prefix(const gchar *prefix)
{
    if (!prefix) return true;
    if (strstr(prefix, "..") != NULL) return false;
    if (strchr(prefix, '/') != NULL || strchr(prefix, '\\') != NULL) return false;
    return true;
}

static gchar *build_capture_base_dir()
{
    // If capture.dir is provided, it must be an absolute path.
    if (cmdArg.cap.dir && cmdArg.cap.dir[0] == '/') {
        return g_strdup(cmdArg.cap.dir);
    }
    return g_strdup_printf("%s/%s", cmdArg.mntDir, cmdArg.cap.path);
}

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    CaptureData *info = (CaptureData *)user_data;

    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d %s", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, __FUNCTION__);
    is_interrupted = TRUE;
}

static GstFlowReturn on_new_sample_from_sink(GstElement *sink, gpointer userData)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstFlowReturn ret;
    CaptureData *info = (CaptureData *)userData;

    /* appsink1에서 샘플 가져오기 */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    
    // We trust the valve. If we got a sample, we forward it.
    // Removed legacy mode and count check.

    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // Buffered capture mode for PNG:
    // - Collect buffers first at a fixed PTS cadence (1/fps)
    // - Then feed them to appsrc later (encode/write can be slow without affecting PTS spacing)
    if (info->buffering_active && info->buffering_req) {
        GstClockTime pts = GST_BUFFER_PTS(buffer);
        gboolean fallback_to_streaming = FALSE;
        bool accept = false;
        {
            std::lock_guard<std::mutex> lock(*info->queue_mutex);

            if (info->buffering_period == 0) {
                // fps may be invalid; avoid division by zero
                info->buffering_period = (info->fps > 0) ? (GST_SECOND / info->fps) : (GST_SECOND / 1);
            }

            if (!GST_CLOCK_TIME_IS_VALID(info->buffering_first_pts) && GST_CLOCK_TIME_IS_VALID(pts)) {
                info->buffering_first_pts = pts;
                info->buffering_next_pts = pts;
            }

            if (!GST_CLOCK_TIME_IS_VALID(pts)) {
                // No timestamp; accept until we reach count
                accept = true;
            } else if (!GST_CLOCK_TIME_IS_VALID(info->buffering_next_pts)) {
                accept = true;
                info->buffering_next_pts = pts;
            } else if (pts + (info->buffering_period / 10) >= info->buffering_next_pts) {
                // Allow small jitter tolerance (10% of period)
                accept = true;
            }

            if (accept) {
                // IMPORTANT: Do NOT keep references to upstream buffers here.
                // v4l2src often uses a small buffer pool; holding GstBuffer refs can
                // starve the pool and trip the upstream watchdog.
                GstBuffer *stored = gst_buffer_copy_deep(buffer);
                if (!stored) {
                    // Fallback to manual copy
                    GstMapInfo map;
                    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                        stored = gst_buffer_new_allocate(NULL, map.size, NULL);
                        if (stored) {
                            gst_buffer_fill(stored, 0, map.data, map.size);
                            GST_BUFFER_PTS(stored) = pts;
                            GST_BUFFER_DTS(stored) = GST_BUFFER_DTS(buffer);
                            GST_BUFFER_DURATION(stored) = GST_BUFFER_DURATION(buffer);
                        }
                        gst_buffer_unmap(buffer, &map);
                    }
                }

                if (!stored) {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffered copy failed; disabling buffering", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
                    info->buffering_active = FALSE;
                    info->buffering_req = nullptr;
                    clear_buffering_buffers(info);
                    fallback_to_streaming = TRUE;
                } else {
                    info->buffering_buffers.push_back(stored);
                }
                if (GST_CLOCK_TIME_IS_VALID(pts)) {
                    info->buffering_next_pts = pts + info->buffering_period;
                }

                if ((gint)info->buffering_buffers.size() >= info->buffering_req->maxCnt) {
                    // Done collecting. Stop upstream flow and start feeding encoder.
                    info->buffering_active = FALSE;
                    if (info->valve) {
                        g_object_set(info->valve, "drop", TRUE, NULL);
                    }

                    if (!info->buffering_feeder_running.load()) {
                        info->buffering_feeder_running.store(true);
                        info->buffering_feeder_thread = g_thread_new("cap-buffer-feed", buffering_feeder_thread, info);
                        __LOG(LOG_INFO, "[%s][%s:%d] ch%d buffered capture: collected %d frames, start feeding", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, info->buffering_req->maxCnt);
                    }
                }
            }
        }

        if (fallback_to_streaming) {
            // Keep streaming behavior for this buffer so capture remains functional.
            GstBuffer *b2 = gst_buffer_ref(buffer);
            g_signal_emit_by_name(info->appsrc, "push-buffer", b2, &ret);
            if (ret != GST_FLOW_OK) {
                __LOG(LOG_ERR, "[%s][%s:%d] ch%d Error pushing buffer to appsrc: %d", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, ret);
            }
        }

        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // [Crash 방지] appsrc 또는 필수 컴포넌트가 NULL이면 즉시 리턴
    if (!info || !info->appsrc) {
        if (sample) gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // Default behavior: forward every sample to appsrc
    g_signal_emit_by_name(info->appsrc, "push-buffer", buffer, &ret);
    if (ret != GST_FLOW_OK)
    {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Error pushing buffer to appsrc: %d", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, ret);
        gst_sample_unref(sample);
        return ret;
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static GstFlowReturn on_new_sample_to_file(GstElement *sink, gpointer userData)
{
    CaptureData *info = (CaptureData *)userData;
    GstSample *sample = NULL;
    const gint queue_limit = (cmdArg.cap.queue_size > 0) ? cmdArg.cap.queue_size : DEFAULT_CAPTURE_QUEUE_SIZE;

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if(!sample) {
        return GST_FLOW_ERROR;
    }

    // [Crash 방지] 유효하지 않은 데이터 접근 차단
    if (!info || !info->queue_mutex) {
        if (sample) gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    bool should_drop = false;
    const gchar *drop_reason = NULL;
    std::shared_ptr<CaptureRequest> req;
    AsyncCaptureTask *task = NULL;

    {
        // Use lock_guard for exception safety (RAII pattern)
        std::lock_guard<std::mutex> lock(*info->queue_mutex);

        // If no current request, try to pop one
        if (!info->current_request) {
            if (!info->request_queue->empty()) {
                info->current_request = info->request_queue->front();
                info->request_queue->pop_front();

                // Reset pushed count for new request
                g_atomic_int_set(&info->captureCnt_, 0);
                info->push_index.store(0);
                info->last_saved_pts = GST_CLOCK_TIME_NONE;

                __LOG(LOG_INFO, "[%s][%s:%d] ch%d Starting new request: %s (max: %d)",
                      CAP_LOG_KEY, _FILE_, __LINE__, info->ch, info->current_request->filePath, info->current_request->maxCnt);
            }
        }

        if (!info->current_request) {
            // Double check if queue is truly empty and close valve if open
            if (info->valve) {
                 g_object_set(info->valve, "drop", TRUE, NULL);
            }
            should_drop = true;
            drop_reason = "no-request";
        } else {
            req = info->current_request;

            // Check queue size limit (prevent memory overflow)
            gint queue_length = g_async_queue_length(info->task_queue);
            if(queue_length > queue_limit) {
                should_drop = true;
                drop_reason = "queue-full";
                req->droppedCnt.fetch_add(1);
            } else {
                // Create async task (transfer ownership of sample to task)
                task = g_new0(AsyncCaptureTask, 1);
                task->sample = sample;
                task->info = info;
                task->request = req; // Link to request

                // Capture sample timestamps for debug/metrics
                task->running_time = get_pipeline_running_time();
                {
                    GstBuffer *b = gst_sample_get_buffer(sample);
                    task->pts = b ? GST_BUFFER_PTS(b) : GST_CLOCK_TIME_NONE;
                }

                // Use push_index which is incremented atomically at push time
                task->capture_index = info->push_index.fetch_add(1);

                // Generate file path for this specific frame
                task->file_path = g_strdup_printf("%s_%d.%s", req->filePath, task->capture_index, info->extention);

                // Push to async queue (non-blocking, fast return)
                g_async_queue_push(info->task_queue, task);

                // Increment pushed count
                gint new_pushed_cnt = g_atomic_int_add(&info->captureCnt_, 1) + 1;

                // Check if we have pushed enough frames for the current request
                if (new_pushed_cnt >= req->maxCnt) {
                    // Current request pushed completely. Release ownership.
                    info->current_request = nullptr;

                    // If no more requests in queue, close the valve to save CPU
                    if (info->request_queue->empty() && info->valve) {
                         g_object_set(info->valve, "drop", TRUE, NULL);
                         __LOG(LOG_INFO, "[%s][%s:%d] ch%d Queue empty, closing valve", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
                    }

                    __LOG(LOG_INFO, "[%s][%s:%d] ch%d Request pushed completely: %s",
                          CAP_LOG_KEY, _FILE_, __LINE__, info->ch, req->filePath);
                }
            }
        }
    } // lock_guard automatically releases mutex here

    if (should_drop) {
        // Drop logging: only when debug is enabled, and throttle to avoid spam.
        // Note: "no-request" drops are expected when not capturing (valve should be closed);
        // treat them as normal and do not warn.
        if (info->debug && drop_reason && g_strcmp0(drop_reason, "no-request") != 0) {
            gint64 now_us = g_get_monotonic_time();
            if (info->last_drop_log_us == 0 || (now_us - info->last_drop_log_us) >= 1000000) {
                info->last_drop_log_us = now_us;

                GstClockTime pts = GST_CLOCK_TIME_NONE;
                GstClockTime rt = get_pipeline_running_time();
                GstClockTime pipe_latency = GST_CLOCK_TIME_NONE;
                {
                    GstBuffer *b = gst_sample_get_buffer(sample);
                    pts = b ? GST_BUFFER_PTS(b) : GST_CLOCK_TIME_NONE;
                    if (GST_CLOCK_TIME_IS_VALID(rt) && GST_CLOCK_TIME_IS_VALID(pts)) {
                        pipe_latency = (rt >= pts) ? (rt - pts) : 0;
                    }
                }

                gint qlen = -1;
                if (info->task_queue) {
                    qlen = g_async_queue_length(info->task_queue);
                }
                gint pushed = g_atomic_int_get(&info->captureCnt_);
                gint dropped_req = req ? (gint)req->droppedCnt.load() : 0;
                gint maxcnt = req ? req->maxCnt : 0;

                __LOG(LOG_WARNING,
                      "[%s][%s:%d] ch%d DROP(%s): pts=%" GST_TIME_FORMAT " pipe=%" GST_TIME_FORMAT " q=%d/%d pushed=%d/%d dropped=%d",
                      CAP_LOG_KEY, _FILE_, __LINE__, info->ch,
                      drop_reason ? drop_reason : "unknown",
                      GST_TIME_ARGS(pts), GST_TIME_ARGS(pipe_latency),
                      qlen, queue_limit, pushed, maxcnt, dropped_req);
            }
        }
        gst_sample_unref(sample);
    }

    return GST_FLOW_OK;
}

void CaptureBin::setAppsrc(GstElement *appsrc)
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__);

    if(appsrc == NULL) __LOG(LOG_ERR, "[%s][%s:%d] ch%d %s : appsrc is NULL!", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__);

    captureData.appsrc = appsrc;

    return;
}

GstState CaptureBin::getState()
{
    GstState state;
    gst_element_get_state(be.bin, &state, NULL, GST_CLOCK_TIME_NONE);

    return state;
}

GstStateChangeReturn CaptureBin::setState(GstState state)
{
    //gst_element_set_state(be.sink, state);
    //gst_element_set_state(be.convert, state);
    //gst_element_set_state(be.crop, state);
    //gst_element_set_state(be.queue, state);
    
    return gst_element_set_state(be.bin, state);
    //return gst_element_change_state(be.bin, GST_STATE_CHANGE_PLAYING_TO_PAUSED);
}

CaptureBin* CaptureBin::getInstance()
{
	static CaptureBin instance;
	return &instance;
}

CaptureBin::CaptureBin()
{
    __LOG(LOG_INFO, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
    sinkPad = NULL;
    be.bin = NULL;
    captureData.captureCnt = cmdArg.cap.maxCnt;
    captureData.captureCnt_ = cmdArg.cap.maxCnt;
    captureData.captureMaxCnt = cmdArg.cap.maxCnt;
    captureData.mode = 0;
    captureData.debug = FALSE;
    captureData.tjCompressor = NULL;
    captureData.enc_type = CAP_ENC_TURBO;

    // Initialize async queue and worker thread
    captureData.task_queue = g_async_queue_new();
    captureData.worker_thread = NULL;
    captureData.worker_running.store(false);
    captureData.push_index.store(0);
    captureData.filePath = NULL;

    // Initialize completion callback
    captureData.complete_callback = NULL;
    captureData.callback_user_data = NULL;
    
    // Initialize Request Queue
    captureData.request_queue = std::unique_ptr<std::deque<std::shared_ptr<CaptureRequest>>>(new std::deque<std::shared_ptr<CaptureRequest>>());
    captureData.queue_mutex = std::unique_ptr<std::mutex>(new std::mutex());
    captureData.current_request = nullptr;
    captureData.last_saved_pts = GST_CLOCK_TIME_NONE;
    captureData.last_drop_log_us = 0;
    captureData.buffering_active = FALSE;
    captureData.buffering_req = nullptr;
    captureData.buffering_first_pts = GST_CLOCK_TIME_NONE;
    captureData.buffering_next_pts = GST_CLOCK_TIME_NONE;
    captureData.buffering_period = 0;
    captureData.buffering_feeder_thread = NULL;
    captureData.buffering_feeder_running.store(false);
    
    timeout_source_id.store(0);
    captureData.valve = NULL;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[%s][%s:%d] %s[%d]", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch);

    // Stop timeout source (must be done from main thread context)
    // Use atomic load/store pattern for thread safety
    guint id = timeout_source_id.load();
    if(id > 0) {
        g_source_remove(id);
        timeout_source_id.store(0);
    }

    // Stop worker thread and wait for completion
    stopWorker();

    // Stop buffered capture feeder thread and free collected buffers
    captureData.buffering_active = FALSE;
    captureData.buffering_req = nullptr;
    captureData.buffering_feeder_running.store(false);
    if (captureData.buffering_feeder_thread) {
        g_thread_join(captureData.buffering_feeder_thread);
        captureData.buffering_feeder_thread = NULL;
    }
    clear_buffering_buffers(&captureData);

    // Clear remaining tasks in queue
    if(captureData.task_queue) {
        AsyncCaptureTask *task;
        while((task = (AsyncCaptureTask *)g_async_queue_try_pop(captureData.task_queue)) != NULL) {
            if(task->sample) gst_sample_unref(task->sample);
            if(task->file_path) g_free(task->file_path);
            g_free(task);
        }
        g_async_queue_unref(captureData.task_queue);
        captureData.task_queue = NULL;
    }
    
    // Clear Request Queue
    if (captureData.queue_mutex) {
        std::lock_guard<std::mutex> lock(*captureData.queue_mutex);
        if (captureData.current_request) {
            captureData.current_request = nullptr;
        }
        if (captureData.request_queue) {
            captureData.request_queue->clear();
        }
    }
    
    if(captureData.filePath) g_free(captureData.filePath);

    destroy_compressor(&captureData);
}
void CaptureBin::addCaptureRequest(gint maxCnt, const gchar *prefix, gpointer userData, guint8 mode, gint timeoutMs)
{
    std::lock_guard<std::mutex> lock(*captureData.queue_mutex);
    
    std::shared_ptr<CaptureRequest> req = std::make_shared<CaptureRequest>();
    req->maxCnt = maxCnt;
    req->userData = userData;
    req->mode = mode;
    req->captureCnt = 0; // Written count
    req->startTime = g_get_monotonic_time(); // microseconds
    // PNG encode path (videoconvert + pngenc) is CPU-heavy. Timeout must scale with
    // requested frame count (maxCnt), otherwise large requests will complete partially.
    if (captureData.enc_type == CAP_ENC_PNG) {
        const gint base_min_ms = 10000; // handle negotiation + first-frame latency
        const gint per_frame_ms = 2500; // conservative budget on target

        gint64 required = base_min_ms;
        if (maxCnt > 0) {
            gint64 scaled = 2000 + (gint64)maxCnt * (gint64)per_frame_ms;
            if (scaled > required) {
                required = scaled;
            }
        }

        if (timeoutMs < required) {
            timeoutMs = (required > G_MAXINT) ? G_MAXINT : (gint)required;
        }
    }
    req->timeoutMs = timeoutMs;
    
    if(prefix != NULL && !is_safe_prefix(prefix)) {
        __LOG(LOG_WARNING, "[%s][%s:%d] ch%d invalid prefix '%s', using default", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, prefix);
        prefix = NULL;
    }

    gchar *base_dir = build_capture_base_dir();

    if(prefix == NULL)
    {
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
        req->filePath = g_strdup_printf("%s/%s_%s-ch%d", base_dir, cmdArg.ohtName, date_str, captureData.ch);
        g_date_time_unref(datetime);
        g_free(date_str);
    }
    else
    {
        req->filePath = g_strdup_printf("%s/%s-ch%d", base_dir, prefix, captureData.ch);
    }

    g_free(base_dir);
    
    captureData.request_queue->push_back(req);
    
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d Added capture request: %s, Queue Size: %ld, Timeout: %d ms", 
          CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, req->filePath, captureData.request_queue->size(), timeoutMs);
          
    // If not capturing, open valve
    if (captureData.valve) {
        g_object_set(captureData.valve, "drop", FALSE, NULL);
    }

    // For PNG multi-frame capture, pre-collect buffers at stable PTS cadence and
    // feed them to appsrc later. This avoids PTS jumps caused by encoder backpressure.
    if (captureData.enc_type == CAP_ENC_PNG && maxCnt > 1 &&
        !captureData.buffering_active && !captureData.current_request) {
        // Clear any leftover buffers
        clear_buffering_buffers(&captureData);

        captureData.buffering_active = TRUE;
        captureData.buffering_req = req;
        captureData.buffering_first_pts = GST_CLOCK_TIME_NONE;
        captureData.buffering_next_pts = GST_CLOCK_TIME_NONE;
        captureData.buffering_period = (captureData.fps > 0) ? (GST_SECOND / captureData.fps) : (GST_SECOND / 1);

        __LOG(LOG_INFO, "[%s][%s:%d] ch%d buffered capture enabled: fps=%d cnt=%d",
              CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, captureData.fps, maxCnt);
    }
}

gboolean CaptureBin::checkTimeout()
{
    gint64 now = g_get_monotonic_time();
    std::lock_guard<std::mutex> lock(*captureData.queue_mutex);
    
    // Check current request
    if (captureData.current_request) {
        gint64 elapsed_ms = (now - captureData.current_request->startTime) / 1000;
        if (elapsed_ms > captureData.current_request->timeoutMs) {
            __LOG(LOG_WARNING, "[%s][%s:%d] ch%d Request TIMEOUT: %s (Elapsed: %ld ms, Limit: %d ms, Cnt: %d/%d)", 
                  CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, captureData.current_request->filePath, 
                  elapsed_ms, captureData.current_request->timeoutMs, 
                  g_atomic_int_get(&captureData.current_request->captureCnt), 
                  captureData.current_request->maxCnt);

            captureData.current_request->timed_out.store(true);

            // Atomic compare-and-swap to prevent double callback
            bool expected = false;
            if (captureData.current_request->responseSent.compare_exchange_strong(expected, true) &&
                captureData.complete_callback) {
                CallbackIdleData *cb_data = g_new0(CallbackIdleData, 1);
                cb_data->callback = captureData.complete_callback;
                cb_data->ch = captureData.ch;
                cb_data->completed_count = g_atomic_int_get(&captureData.current_request->captureCnt);
                cb_data->user_data = captureData.current_request->userData;
                g_idle_add(invoke_complete_callback_idle, cb_data);
            }
            
            // Force move to next request
            captureData.current_request = nullptr;
            
            // If queue is empty, close valve
            if (captureData.request_queue->empty() && captureData.valve) {
                 g_object_set(captureData.valve, "drop", TRUE, NULL);
                 __LOG(LOG_INFO, "[%s][%s:%d] ch%d Timeout cleared queue, closing valve", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
            }
        }
    }
    
    // Check pending queue for expired requests (they haven't even started yet)
    // We iterate safely
    auto it = captureData.request_queue->begin();
    while (it != captureData.request_queue->end()) {
        std::shared_ptr<CaptureRequest> req = *it;
        gint64 elapsed_ms = (now - req->startTime) / 1000;
        
        if (elapsed_ms > req->timeoutMs) {
             __LOG(LOG_WARNING, "[%s][%s:%d] ch%d Pending Request TIMEOUT: %s (Elapsed: %ld ms)",
                   CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, req->filePath, elapsed_ms);

             req->timed_out.store(true);

             // If this timed-out request was the current buffering target,
             // stop buffering and drop collected frames to avoid misattribution.
             if (captureData.buffering_req && captureData.buffering_req.get() == req.get()) {
                 captureData.buffering_active = FALSE;
                 captureData.buffering_feeder_running.store(false);
                 captureData.buffering_req = nullptr;
                 clear_buffering_buffers(&captureData);

                 __LOG(LOG_INFO, "[%s][%s:%d] ch%d Cleared buffered capture for timed-out request",
                       CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
             }

             // Atomic compare-and-swap to prevent double callback
             bool expected = false;
             if (req->responseSent.compare_exchange_strong(expected, true) &&
                 captureData.complete_callback) {
                CallbackIdleData *cb_data = g_new0(CallbackIdleData, 1);
                cb_data->callback = captureData.complete_callback;
                cb_data->ch = captureData.ch;
                cb_data->completed_count = 0; // Not started
                cb_data->user_data = req->userData;
                g_idle_add(invoke_complete_callback_idle, cb_data);
            }
            
            it = captureData.request_queue->erase(it);
        } else {
            ++it;
        }
    }
    
    return G_SOURCE_CONTINUE;
}

// Kept for backward compatibility but modified logic
gint CaptureBin::startCapture(gint maxCnt)
{
    // Redirect to addCaptureRequest with NULL userData and current filePath
    // Note: This legacy method assumes filePath was set by setFilePath
    addCaptureRequest(maxCnt, NULL, NULL, 1, 5000); // Default 5s timeout
    
    // Override the filePath in the request we just added if setFilePath was called before
    // (This is messy, better to fix caller, but for safety:)
    if (captureData.filePath) {
        // Find the last request
        std::lock_guard<std::mutex> lock(*captureData.queue_mutex);
        if (!captureData.request_queue->empty()) {
            std::shared_ptr<CaptureRequest> req = captureData.request_queue->back();
            if (req->filePath) g_free(req->filePath);
            req->filePath = g_strdup(captureData.filePath);
        }
    }
    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__);

    // Close valve first
    if(captureData.valve) {
        g_object_set(captureData.valve, "drop", TRUE, NULL);
        __LOG(LOG_INFO, "[%s][%s:%d] ch%d valve closed", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
    }
    
    std::lock_guard<std::mutex> lock(*captureData.queue_mutex);

    // Clear current request first
    if (captureData.current_request) {
        captureData.current_request = nullptr;
    }

    // Clear pending requests
    if (captureData.request_queue) {
         captureData.request_queue->clear();
    }

    // Stop buffered capture feeder thread and drop any collected buffers
    captureData.buffering_active = FALSE;
    captureData.buffering_req = nullptr;
    captureData.buffering_feeder_running.store(false);
    if (captureData.buffering_feeder_thread) {
        g_thread_join(captureData.buffering_feeder_thread);
        captureData.buffering_feeder_thread = NULL;
    }
    clear_buffering_buffers(&captureData);

    return 1;
}

void CaptureBin::setMaxCnt(guint16 maxCnt)
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d setMaxCnt %d", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, maxCnt);
    captureData.captureMaxCnt = maxCnt;
}

void CaptureBin::startWorker()
{
    if(!captureData.worker_thread) {
        captureData.worker_running.store(true);
        captureData.worker_thread = g_thread_new("capture_worker", capture_worker_thread, &captureData);
        __LOG(LOG_INFO, "[%s][%s:%d] ch%d worker thread created", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
    }
}

void CaptureBin::stopWorker()
{
    if(captureData.worker_thread) {
        captureData.worker_running.store(false);
        g_thread_join(captureData.worker_thread);
        captureData.worker_thread = NULL;
        __LOG(LOG_INFO, "[%s][%s:%d] ch%d worker thread joined", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
    }
}

gboolean CaptureBin::isQueueEmpty()
{
    if(!captureData.task_queue) return TRUE;
    return (g_async_queue_length(captureData.task_queue) == 0);
}

void CaptureBin::setCompleteCallback(CaptureCompleteCallback callback, gpointer user_data)
{
    captureData.complete_callback = callback;
    captureData.callback_user_data = user_data;
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d completion callback %s",
          CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, callback ? "registered" : "cleared");
}

void CaptureBin::setMode(guint8 mode)
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d setMode %d", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, mode);
    captureData.mode = mode;
}

guint8 CaptureBin::getMode()
{
    return captureData.mode;
}

guint8 CaptureBin::getFPS()
{
    return captureData.fps;
}

void CaptureBin::setTimeStampDebug()
{
    captureData.debug = !captureData.debug;
}

gint CaptureBin::getCaptureCnt()
{
    return captureData.captureCnt;
}

gint CaptureBin::getCaptureCnt_()
{
    return captureData.captureCnt_;
}

const gchar* CaptureBin::getCaptureFilePath()
{
    return captureData.filePath;
}

void CaptureBin::setQueueSize(guint size)
{
    //g_object_get(re.enc, "bitrate", &bps, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch%d get bitrate : %d", _FILE_, __LINE__, ch, bps);

    g_object_set(be.queue, "max-size-buffers", size, NULL);
    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d set gop_size : %d", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, size);
    g_print("rtsp ch%d set gop_size : %d\n", captureData.ch, size);
}

gboolean CaptureBin::addBinToPipe(GstElement *pipe)
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d %s, add_cap_f:%d", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__, add_cap_f);
    add_cap_f = TRUE;

    if(gst_bin_get_by_name(GST_BIN(pipe), g_strdup_printf("captureBin%d", captureData.ch)) != NULL)
    {
        __LOG(LOG_INFO, "[%s][%s:%d] ch%d capture bin is already added", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
        return 1;
    }

    return gst_bin_add(GST_BIN(pipe), be.bin);
}

gboolean CaptureBin::removeBinToPipe(GstElement *pipe)
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d %s, add_cap_f:%d", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__, add_cap_f);
    add_cap_f = FALSE;

    return gst_bin_remove(GST_BIN(pipe), be.bin);
}

GstPad* CaptureBin::getBinSinkPad()
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, __FUNCTION__);
    //return gst_element_get_static_pad(re.bin, g_strdup_printf("recordBin_sink_ch%d", ch));
    return sinkPad;
}

gint CaptureBin::setFilePath(const gchar *prefix)
{
    if(captureData.filePath) g_free(captureData.filePath);

    gchar *base_dir = build_capture_base_dir();
    
    if(prefix == NULL)
    {
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

        captureData.filePath = g_strdup_printf("%s/%s_%s-ch%d", base_dir, cmdArg.ohtName, date_str, captureData.ch);

        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);

        g_date_time_unref(datetime);
        g_free(date_str);
    }
    else
    {
        captureData.filePath = g_strdup_printf("%s/%s-ch%d", base_dir, prefix, captureData.ch);
        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);
    }

    g_free(base_dir);

    return 1;
}

gboolean CaptureBin::init(guint8 ch)
{
    gboolean ret = 0;
    GstPad *staticPad = NULL;
    GstCaps *caps = NULL;
    GstCaps *srccaps = NULL;
    gboolean crop_en = cmdArg.crop_en[ch/2];
    captureData.ch = ch;
    captureData.fps = cmdArg.fps[STREAM_CAP][captureData.ch];
    captureData.quality = cmdArg.cap.quality;

    // Determine encoder type before element creation
    captureData.enc_type = CAP_ENC_JPEG;
    captureData.extention = "jpg";
    if(g_strcmp0(cmdArg.cap.encoder, "jpeg") == 0 || g_strcmp0(cmdArg.cap.encoder, "jpg") == 0) {
        captureData.enc_type = CAP_ENC_JPEG;
        captureData.extention = "jpg";
    }
    else if(g_strcmp0(cmdArg.cap.encoder, "turbo") == 0 || g_strcmp0(cmdArg.cap.encoder, "turbojpeg") == 0 || g_strcmp0(cmdArg.cap.encoder, "turbojpg") == 0) {
        captureData.enc_type = CAP_ENC_TURBO;
        captureData.extention = "jpg";
        captureData.tjCompressor = tjInitCompress();
    }
    else if(g_strcmp0(cmdArg.cap.encoder, "raw") == 0) {
        captureData.enc_type = CAP_ENC_RAW;
        captureData.extention = "raw";
    }
    else if(g_strcmp0(cmdArg.cap.encoder, "png") == 0) {
        captureData.enc_type = CAP_ENC_PNG;
        captureData.extention = "png";
    }
    else if(cmdArg.cap.encoder != NULL) {
        __LOG(LOG_ERR, "[%s][%s:%d] capture enc %s is invalid : default enc type is jpeg", CAP_LOG_KEY, _FILE_, __LINE__, cmdArg.cap.encoder);
        captureData.enc_type = CAP_ENC_JPEG;
        captureData.extention = "jpg";
    }
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[%s][%s:%d] %s ch : %d, crop : %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", captureData.ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue_%d", captureData.ch));
    be.valve = gst_element_factory_make("valve", g_strdup_printf("valve_%d", captureData.ch));
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue2_%d", captureData.ch));
    // PNG needs strict raw formats (RGB/RGBA). Prefer software videoconvert for broad compatibility.
    // If videoconvert is not available, fall back to imxvideoconvert_g2d.
    if (captureData.enc_type == CAP_ENC_PNG) {
        be.imx_convert = gst_element_factory_make("videoconvert", g_strdup_printf("convert%d", captureData.ch));
        if (!be.imx_convert) {
            __LOG(LOG_WARNING, "[%s][%s:%d] videoconvert not available; fallback to imxvideoconvert_g2d", CAP_LOG_KEY, _FILE_, __LINE__);
            be.imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imx_convert%d", captureData.ch));
        }
    }
    else {
        be.imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imx_convert%d", captureData.ch));
    }
    be.enc = NULL;
    if (captureData.enc_type == CAP_ENC_JPEG) {
        be.enc = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", captureData.ch));
    } else if (captureData.enc_type == CAP_ENC_PNG) {
        be.enc = gst_element_factory_make("pngenc", g_strdup_printf("pngenc%d", captureData.ch));
        if (!be.enc) {
            __LOG(LOG_CRIT, "[%s][%s:%d] pngenc not available (gst-plugins-good required)", CAP_LOG_KEY, _FILE_, __LINE__);
            return ret;
        }
    }
    be.appsink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", captureData.ch));
    be.crop = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", captureData.ch));
    be.overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", captureData.ch));
    be.capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", captureData.ch));
    be.queue_sink = gst_element_factory_make("appsink", g_strdup_printf("queue_sink%d", captureData.ch));
    be.appsrc = gst_element_factory_make("appsrc", g_strdup_printf("queue_src%d", captureData.ch));

    //GstElement *imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imxconvert%d", captureData.ch));

    if (!be.bin || !be.queue || !be.valve || !be.queue2 || !be.appsink || !be.imx_convert || !be.crop || !be.overlay || \
        !be.capsfilter || !be.appsrc || !be.queue_sink || \
        ((captureData.enc_type == CAP_ENC_JPEG || captureData.enc_type == CAP_ENC_PNG) && !be.enc)) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture element create error", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.valve, be.queue2, be.imx_convert, be.appsink, be.crop, be.overlay, \
                    be.capsfilter, be.queue_sink, be.appsrc, NULL);
    if (be.enc) {
        gst_bin_add(GST_BIN(be.bin), be.enc);
    }
    //gst_bin_add_many(GST_BIN(be.bin), be.queue_src, be.queue3, be.queue_sink, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), imx_convert, , NULL);

#if 0
    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture bin add error in pipeline", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }
#endif

    //ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    ret = gst_element_link_many(be.queue, be.valve, be.queue_sink, NULL);
    if (!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture queue->valve->queue_sink link err", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    // Set valve to drop buffers by default (when not capturing) for zero CPU usage
    g_object_set(be.valve, "drop", TRUE, NULL);
    captureData.valve = be.valve;

#if 1
    switch(captureData.enc_type)
    {
        case CAP_ENC_JPEG:
            if(crop_en) ret = gst_element_link_many(be.appsrc, be.crop, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.appsink, NULL);
            else ret = gst_element_link_many(be.appsrc, be.capsfilter, be.enc, be.queue2, be.appsink, NULL);
            caps = gst_caps_new_simple("video/x-raw",
                                        //"format", G_TYPE_STRING, "RGBx",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                        NULL);
            g_object_set(be.capsfilter, "caps", caps, NULL);
            gst_caps_unref(caps);
            g_object_set(be.enc, "quality", captureData.quality, NULL);
            break;
        case CAP_ENC_PNG:
            // pngenc expects RGB/RGBA/GRAY*. Force conversion to RGBA (more widely supported than RGB).
            if(crop_en) ret = gst_element_link_many(be.appsrc, be.crop, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.appsink, NULL);
            else ret = gst_element_link_many(be.appsrc, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.appsink, NULL);
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGBA",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                        NULL);
            g_object_set(be.capsfilter, "caps", caps, NULL);
            gst_caps_unref(caps);

            // Map capture quality (0-100, default 85) to pngenc compression-level (0-9).
            // PNG is lossless; "quality" here controls speed vs file size:
            // - quality 100 => compression-level 0 (fastest, largest)
            // - quality 0   => compression-level 9 (slowest, smallest)
            {
                gint q = captureData.quality;
                if (q < 0) q = 0;
                if (q > 100) q = 100;
                guint comp = (guint)(((100 - q) * 9 + 50) / 100);
                g_object_set(be.enc, "compression-level", comp, NULL);
                __LOG(LOG_INFO, "[%s][%s:%d] ch%d pngenc compression-level=%u (quality=%d)",
                      CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch, comp, q);
            }
            break;
        case CAP_ENC_TURBO:
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGBx",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                        NULL);
            g_object_set(be.capsfilter, "caps", caps, NULL);
            gst_caps_unref(caps);
            if(crop_en) ret = gst_element_link_many(be.appsrc, be.crop, be.imx_convert, be.capsfilter, be.queue2, be.appsink, NULL);
            else ret = gst_element_link_many(be.appsrc, be.capsfilter, be.queue2, be.appsink, NULL);
            break;
        case CAP_ENC_RAW:
            caps = gst_caps_new_simple("video/x-raw",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                        NULL);
            g_object_set(be.capsfilter, "caps", caps, NULL);
            gst_caps_unref(caps);
            if(crop_en) ret = gst_element_link_many(be.appsrc, be.crop, be.imx_convert, be.capsfilter, be.queue2, be.appsink, NULL);
            else ret = gst_element_link_many(be.appsrc, be.queue2, be.appsink, NULL);
            break;
        default:
            __LOG(LOG_CRIT, "[%s][%s:%d] capture encoder type is invalid : %d ", CAP_LOG_KEY, _FILE_, __LINE__, captureData.enc_type);
            return -1;
            break;
    }

#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture queue_sink link err", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    g_object_set(be.appsrc, "is-live", TRUE, NULL);
    g_object_set(be.appsrc, "format", GST_FORMAT_TIME, NULL);
    captureData.appsrc = be.appsrc;

#if 1
    if(crop_en)
    {
        srccaps = gst_caps_new_simple("video/x-raw",
                                    "format", G_TYPE_STRING, "RGBx",
                                    "width", G_TYPE_INT, cmdArg.width*(crop_en+1),
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                    NULL);
        g_object_set(be.appsrc, "caps", srccaps, NULL);
        gst_caps_unref(srccaps);
    }
    else
    {
        srccaps = gst_caps_new_simple("video/x-raw",
                                    "format", G_TYPE_STRING, "RGBx",
                                    "width", G_TYPE_INT, cmdArg.width*(crop_en+1),
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                    NULL);
        g_object_set(be.appsrc, "caps", srccaps, NULL);
        gst_caps_unref(srccaps);
    }
#endif

#if 0
    queue_src_pad = gst_element_get_static_pad(be.queue3, "src");
    gst_pad_add_probe(queue_src_pad, GST_PAD_PROBE_TYPE_BUFFER, queue_probe_callback, &captureData, NULL);
    gst_object_unref(queue_src_pad);
#endif
    // queue_sink properties: no drop (valve handles dropping), no sync for max speed
    g_object_set(be.queue_sink, "drop", FALSE, NULL);
    g_object_set(be.queue_sink, "emit-signals", TRUE, "sync", FALSE, "async", FALSE, NULL);
    //g_signal_connect(be.queue_sink, "eos", G_CALLBACK(eos_callback2), &captureData);
    g_signal_connect(be.queue_sink, "new-sample", G_CALLBACK(on_new_sample_from_sink), &captureData);
    //g_signal_connect(be.queue_sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

#if 0
    GstCaps *templ = gst_pad_get_pad_template_caps(gst_element_get_static_pad(be.convert, "sink"));
    caps = gst_caps_copy(templ);
    gst_caps_set_simple(caps, "format", G_TYPE_STRING, "RGBx", NULL);
    gst_caps_fixate(caps);
    gst_caps_unref(caps);

    GstCaps *temp2 = gst_pad_get_pad_template_caps(gst_element_get_static_pad(be.convert, "src"));
    caps = gst_caps_copy(temp2);
    gst_caps_set_simple(caps, "format", G_TYPE_STRING, "I420", NULL);
    gst_caps_fixate(caps);
    gst_caps_unref(caps);
#endif

    if (captureData.ch % 2 == 0)
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
    else
        g_object_set(be.crop, "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);

    //if(cmdArg.rtsp_fps >= 25) g_ ject_set(re.rate, "max-rate", cmdArg.rtsp_fps, "drop-only", TRUE, NULL);
    //queue_src_pad = gst_element_get_static_pad(be.queue, "src");
    //probe_id = gst_pad_add_probe(queue_src_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, NULL, NULL, NULL);
    //g_object_set(re.enc, "bitrate", cmdArg.rtsp_bitrate, NULL);
    
    // [Queue 최적화] CaptureBin 대기열 축소 (JSON 설정값 사용), 최신 프레임 우선 (Leaky)
    g_object_set(be.queue, "max-size-time", cmdArg.queue_cap_src_time_ms*GST_MSECOND, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", cmdArg.queue_cap_src_time_ms*GST_MSECOND, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    //g_object_set(be.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(be.sink, "async", TRUE, NULL);
    //g_object_set(be.appsink, "max-buffers", captureData.fps/2, NULL);
    // appsink properties: no drop (valve handles dropping), no sync for max speed
    g_object_set(be.appsink, "drop", FALSE, NULL);
    g_object_set(be.appsink, "emit-signals", TRUE, "sync", FALSE, "async", FALSE, NULL);
    g_signal_connect(be.appsink, "eos", G_CALLBACK(eos_callback), &captureData);
    g_signal_connect(be.appsink, "new-sample", G_CALLBACK(on_new_sample_to_file), &captureData);

    //g_signal_connect(be.sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

    staticPad = gst_element_get_static_pad(be.queue, "sink");
    sinkPad = gst_ghost_pad_new(g_strdup_printf("captureBin_sink_ch%d", captureData.ch), staticPad);

    ret = gst_element_add_pad(be.bin, sinkPad);
    if(!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture pad add error in bin", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    //g_strdup_printf(captureData.filePath, "/mnt/sd_cam/capture/default-ch%d", captureData.ch);

    gst_object_unref(staticPad);

    // Start async worker thread for background file I/O
    startWorker();
    
    // Start timeout monitor
    timeout_source_id.store(g_timeout_add(100, timeout_check_func, this));

    return ret;
}
