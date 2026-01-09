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
};

// Worker thread for async file I/O
static gpointer capture_worker_thread(gpointer user_data)
{
    CaptureData *info = (CaptureData *)user_data;

    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d worker thread started", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);

    while(info->worker_running || g_async_queue_length(info->task_queue) > 0)
    {
        // Wait for task with 100ms timeout
        AsyncCaptureTask *task = (AsyncCaptureTask *)g_async_queue_timeout_pop(info->task_queue, 100000);
        if(!task) continue;

        GstBuffer *buffer = NULL;
        FILE *fp = NULL;
        GstMapInfo map;
        gboolean success = FALSE;

        do {
            if(task->info->enc_type == CAP_ENC_TURBO)
            {
                long jpeg_size = 0;
                unsigned char *jpeg_buf = compress_frame_to_jpeg(task->sample, &jpeg_size, task->info);
                if(jpeg_buf && jpeg_size > 0) {
                    fp = fopen(task->file_path, "wb");
                    if(fp) {
                        fwrite(jpeg_buf, 1, jpeg_size, fp);
                        fclose(fp);
                        __LOG(LOG_INFO, "[%s][%s:%d] ch%d Saved JPEG to %s (%ld bytes)", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path, jpeg_size);
                        success = TRUE;
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
                    fwrite(map.data, 1, map.size, fp);
                    fclose(fp);
                    success = TRUE;
                }
                else {
                    __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to open file %s for writing", CAP_LOG_KEY, _FILE_, __LINE__, task->info->ch, task->file_path);
                }
                gst_buffer_unmap(buffer, &map);
            }
        } while(0);

        // Increment counter only after successful write (atomic operation)
        if(success) {
            g_atomic_int_inc(&task->info->captureCnt_);
        }

        // Cleanup
        if(task->sample) gst_sample_unref(task->sample);
        if(task->file_path) g_free(task->file_path);
        g_free(task);
    }

    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d worker thread stopped", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
    return NULL;
}

static inline void tiny_sleep_ns(long ns) {
    struct timespec ts; ts.tv_sec = ns / 1000000000L; ts.tv_nsec = ns % 1000000000L;
    nanosleep(&ts, NULL);
}

#if 0
static ssize_t write_all_with_retry(int fd, const void *buf, size_t len,
                                    int max_retries /* e.g., 4 */) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t left = len;
    int attempt = 0;
    while (left > 0) {
        ssize_t wr = write(fd, p, left);
        if (wr > 0) { p += wr; left -= (size_t)wr; continue; }

        // wr <= 0
        if (wr < 0 && errno == EINTR) continue; // 즉시 재시도

        // 일시적 오류(네트워크/파이프 등) → 짧은 백오프 후 재시도
        if (wr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                       errno == ETIMEDOUT /* 가능성 */)) {
            if (attempt++ < max_retries) { tiny_sleep_ns( (1L<<attempt) * 1000000L ); continue; } // 1/2/4/8 ms
        }
        // 재시도 무의미한 에러
        return -1;
    }
    return (ssize_t)len;
}
#endif

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    CaptureData *info = (CaptureData *)user_data;

    __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d %s", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, __FUNCTION__);
    is_interrupted = TRUE;
}

#if 0
static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) 
{
    __LOG(LOG_INFO, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);

    return GST_FLOW_OK;
}

static GstPadProbeReturn queue_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer userData)
{
    CaptureData *arg = (CaptureData *)userData;

#if 0
    if(arg->appsrc == NULL || GST_STATE(GST_ELEMENT(arg->appsrc)) != GST_STATE_PLAYING)
    {
        g_print("appsrc null return!\n");
        return GST_PAD_PROBE_OK;
    }
#endif

    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if(buffer) gst_app_src_push_buffer(GST_APP_SRC(arg->appsrc), gst_buffer_copy(buffer));

    return GST_PAD_PROBE_OK;
}
#endif

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
    
    //g_print("pull\n");
#if 1
    if(info->mode == 0) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    if(info->captureCnt > info->captureMaxCnt)
    {
        if(info->mode == 2)
        {
            info->captureCnt = 0;
            __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d capture cnt reset(fromsink)", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        }
        else
        {
            //g_usleep(1000000);
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }
    info->captureCnt++;
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d capture cnt : %d", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, info->captureCnt);
#endif
    
#if 0
    if(info->appsrc == NULL || GST_STATE(GST_ELEMENT(info->appsrc)) != GST_STATE_PLAYING)
    {
        //g_print("appsrc null return!\n");
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
#endif

    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    g_signal_emit_by_name(info->appsrc, "push-buffer", buffer, &ret);
    if (ret != GST_FLOW_OK)
    {
        //g_printerr("Error pushing buffer to appsrc: %d\n", ret);
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Error pushing buffer to appsrc: %d", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, ret);
        return ret;
    }

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

#if 0
static GstFlowReturn on_new_sample_to_file(GstElement *sink, gpointer userData) 
{
    GstSample *sample;
    GstBuffer *buffer;
    CaptureData *info = (CaptureData *)userData;
    //guint8 mode;
    GstMapInfo map;
    FILE *file;
    gchar *path = NULL;
    gchar *extention = NULL;

    //g_print("%s\n", __FUNCTION__);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }

#if 1
    if(info->captureCnt_ >= info->captureMaxCnt)
    {
        if(info->mode == 1)
        {
            info->captureCnt_ = 0;
            __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d capture cnt reset(tofile)", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        }
        else
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }
#endif

    //gst_sample_unref(sample);
#if 0
    if(info->appsrc == NULL || GST_STATE(GST_ELEMENT(info->appsrc)) != GST_STATE_PLAYING)
    {
        //g_print("appsrc null return!\n");
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
#endif

    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    //GstBuffer *copied_buffer = gst_buffer_copy(buffer);
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        //g_printerr("Failed to map buffer\n");
        __LOG(LOG_ERR, "[%s][%s:%d] ch%d Failed to map buffer", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

#if 0
    info->buf = gst_buffer_new_and_alloc(map.size);
    gst_buffer_fill(info->buf, 0, map.data, map.size);
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
#endif

    //gst_sample_unref(sample);

    if(info->debug)
    {
        //if(info->ch == 0)
        {
            //g_message("ch%d Timestamp: %" GST_TIME_FORMAT "\n", info->ch, GST_TIME_ARGS(timestamp));
            unsigned char md5_result[MD5_DIGEST_LENGTH];
            MD5(map.data, map.size, md5_result);

            // MD5 해시 값을 로그로 출력
            char md5_string[MD5_DIGEST_LENGTH * 2 + 1] = {0};
            for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
                sprintf(&md5_string[i * 2], "%02x", md5_result[i]);
            }
            GstClockTime timestamp = GST_BUFFER_PTS(buffer);
            g_message("ch%d Timestamp: %" GST_TIME_FORMAT " MD5 Hash: %s\n", info->ch, GST_TIME_ARGS(timestamp), md5_string);
        }
    }

    //g_print("captureCnt %d, captureMax %d\n", info->captureCnt, info->captureMaxCnt);
    if(cmdArg.cap_encoder_en) extention = g_strdup_printf("%s", "jpg");
    else extention = g_strdup_printf("%s", "rgb");

    //if(info->captureMaxCnt > 1 && info->captureMaxCnt <= cmdArg.fps[STREAM_CAP][info->ch]) 
    {
        path = g_strdup_printf("%s_%d.%s", info->filePath, info->captureCnt_++, extention);
        //__LOG(LOG_DEBUG, "[GST][%s:%d] path : %s, cnt : %d, max : %d", _FILE_, __LINE__, info->filePath, info->captureCnt, info->captureMaxCnt);
    }
    //else path = g_strdup_printf("%s.%s", info->filePath, extention);

    file = fopen(path, "wb");   //fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[%s][%s:%d] %s file open error", CAP_LOG_KEY, _FILE_, __LINE__, path);
    }

    if(path != NULL) g_free(path);
    if(extention != NULL) g_free(extention);
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}
#endif

static GstFlowReturn on_new_sample_to_file(GstElement *sink, gpointer userData)
{
    CaptureData *info = (CaptureData *)userData;
    GstSample *sample = NULL;

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if(!sample) {
        return GST_FLOW_ERROR;
    }

    // Check if capture is complete
    gint current_cnt = g_atomic_int_get(&info->captureCnt_);
    if(current_cnt >= info->captureMaxCnt)
    {
        if(info->mode == 2)
        {
            g_atomic_int_set(&info->captureCnt_, 0);
            __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d capture cnt reset(tofile)", CAP_LOG_KEY, _FILE_, __LINE__, info->ch);
        }
        else
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }

    // Check queue size limit (prevent memory overflow)
    gint queue_length = g_async_queue_length(info->task_queue);
    if(queue_length > 30) {
        __LOG(LOG_WARNING, "[%s][%s:%d] ch%d queue full (%d), dropping frame", CAP_LOG_KEY, _FILE_, __LINE__, info->ch, queue_length);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // Create async task (transfer ownership of sample to task)
    AsyncCaptureTask *task = g_new0(AsyncCaptureTask, 1);
    task->sample = sample;
    task->info = info;
    // Use push_index which is incremented atomically at push time (not after file write)
    // This ensures sequential file indexing regardless of worker thread processing order
    task->capture_index = g_atomic_int_add(&info->push_index, 1);
    task->file_path = g_strdup_printf("%s_%d.%s", info->filePath, task->capture_index, info->extention);

    // Push to async queue (non-blocking, fast return)
    g_async_queue_push(info->task_queue, task);

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
    captureData.worker_running = FALSE;
    captureData.push_index = 0;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[%s][%s:%d] %s[%d]", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch);

    // Stop worker thread and wait for completion
    stopWorker();

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

    destroy_compressor(&captureData);
}

gint CaptureBin::startCapture(gint maxCnt)
{
    //if (getBinSinkPad() == NULL) return 0;
    //setFilePath();
    //captureData.mode = mode;
    captureData.captureMaxCnt = maxCnt;
    captureData.captureCnt = 0;
    captureData.captureCnt_ = 0;
    g_atomic_int_set(&captureData.push_index, 0);  // Reset push index for new capture
    __LOG(LOG_INFO, "[%s][%s:%d] %s cnt:%d, maxCnt:%d", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.captureCnt, captureData.captureMaxCnt);

    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
    captureData.captureMaxCnt = cmdArg.cap.maxCnt;
    captureData.captureCnt = 0;
    captureData.captureCnt_ = 0;
    captureData.mode = 0;

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
        captureData.worker_running = TRUE;
        captureData.worker_thread = g_thread_new("capture_worker", capture_worker_thread, &captureData);
        __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d worker thread created", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
    }
}

void CaptureBin::stopWorker()
{
    if(captureData.worker_thread) {
        captureData.worker_running = FALSE;
        g_thread_join(captureData.worker_thread);
        captureData.worker_thread = NULL;
        __LOG(LOG_NOTICE, "[%s][%s:%d] ch%d worker thread joined", CAP_LOG_KEY, _FILE_, __LINE__, captureData.ch);
    }
}

gboolean CaptureBin::isQueueEmpty()
{
    if(!captureData.task_queue) return TRUE;
    return (g_async_queue_length(captureData.task_queue) == 0);
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

gint CaptureBin::setFilePath(guint8 *prefix)
{
    if(prefix == NULL)
    {
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

        captureData.filePath = g_strdup_printf("%s/%s/%s_%s-ch%d", cmdArg.mntDir, cmdArg.cap.path, cmdArg.ohtName, date_str, captureData.ch);

        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);

        g_date_time_unref(datetime);
        g_free(date_str);
    }
    else
    {
        captureData.filePath = g_strdup_printf("%s/%s/%s-ch%d", cmdArg.mntDir, cmdArg.cap.path, prefix, captureData.ch);
        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);
    }

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
    //sinkPad = NULL;
    __LOG(LOG_INFO, "[%s][%s:%d] %s ch : %d, crop : %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", captureData.ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue_%d", captureData.ch));
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue2_%d", captureData.ch));
    be.imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imx_convert%d", captureData.ch));
    be.enc = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", captureData.ch));
    be.appsink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", captureData.ch));
    be.crop = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", captureData.ch));
    be.overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", captureData.ch));
    be.capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", captureData.ch));
    be.queue_sink = gst_element_factory_make("appsink", g_strdup_printf("queue_sink%d", captureData.ch));
    be.appsrc = gst_element_factory_make("appsrc", g_strdup_printf("queue_src%d", captureData.ch));

    //GstElement *imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imxconvert%d", captureData.ch));

    if (!be.bin || !be.queue || !be.queue2 || !be.enc || !be.appsink || !be.imx_convert || !be.crop || !be.overlay || \
        !be.capsfilter || !be.appsrc || !be.queue_sink) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture element create error", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.queue2, be.imx_convert, be.enc, be.appsink, be.crop, be.overlay, \
                    be.capsfilter, be.queue_sink, be.appsrc, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), be.queue_src, be.queue3, be.queue_sink, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), imx_convert, , NULL);

#if 0
    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }
#endif

    //ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    ret = gst_element_link(be.queue, be.queue_sink);
    if (!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture queue_sink link err", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    if(g_strcmp0(cmdArg.cap.encoder, "jpeg") == 0 || g_strcmp0(cmdArg.cap.encoder, "jpg") == 0) {
        captureData.enc_type = CAP_ENC_JPEG;
        captureData.extention = g_strdup_printf("%s", "jpg");
    }
    else if(g_strcmp0(cmdArg.cap.encoder, "turbo") == 0 || g_strcmp0(cmdArg.cap.encoder, "turbojpeg") == 0 || g_strcmp0(cmdArg.cap.encoder, "turbojpg") == 0) {
        captureData.enc_type = CAP_ENC_TURBO;
        captureData.extention = g_strdup_printf("%s", "jpg");
        captureData.tjCompressor = tjInitCompress();
    }
    else if(g_strcmp0(cmdArg.cap.encoder, "raw") == 0) {
        captureData.enc_type = CAP_ENC_RAW;
        captureData.extention = g_strdup_printf("%s", "raw");
    }
    else {
        __LOG(LOG_ERR, "[%s][%s:%d] capture enc %s is invalid : default enc type is jpeg", CAP_LOG_KEY, _FILE_, __LINE__, cmdArg.cap.encoder);
    }

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
                                    //"framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                    NULL);
        g_object_set(be.appsrc, "caps", srccaps, NULL);
        gst_caps_unref(srccaps);
    }
    else
    {
        srccaps = gst_caps_new_simple("video/x-raw",
                                    //"format", G_TYPE_STRING, "RGBx",
                                    "width", G_TYPE_INT, cmdArg.width*(crop_en+1),
                                    "height", G_TYPE_INT, cmdArg.height,
                                    //"framerate", GST_TYPE_FRACTION, captureData.fps, 1,
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
    g_object_set(be.queue_sink, "drop", TRUE, NULL);
    g_object_set(be.queue_sink, "emit-signals", TRUE, "sync", TRUE, "async", FALSE, NULL);
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
    
    g_object_set(be.queue, "max-size-time", GST_SECOND/2, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND/2, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    //g_object_set(be.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(be.sink, "async", TRUE, NULL);
    //g_object_set(be.appsink, "max-buffers", captureData.fps/2, NULL);
    g_object_set(be.appsink, "drop", TRUE, NULL);
    g_object_set(be.appsink, "emit-signals", TRUE, "sync", TRUE, "async", FALSE, NULL);
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

    return ret;
}