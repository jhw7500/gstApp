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
        __LOG(LOG_ERR, "[%s][%s:%d] Only RGB format supported here. Current format: %s", CAP_LOG_KEY, _FILE_, __LINE__, format);
        return NULL;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        __LOG(LOG_ERR, "[%s][%s:%d] Failed to map buffer", CAP_LOG_KEY, _FILE_, __LINE__);
        return NULL;
    }

    if (!init_compressor(info)) {
        gst_buffer_unmap(buffer, &map);
        return NULL;
    }

    unsigned char *jpeg_buf = NULL;
    int pixel_format = TJPF_RGBX;
    int jpeg_quality = 85; // 1~100
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
        __LOG(LOG_ERR, "[%s][%s:%d] JPEG compression failed: %s", CAP_LOG_KEY, _FILE_, __LINE__, tjGetErrorStr());
        if (jpeg_buf) tjFree(jpeg_buf);
        return NULL;
    }

    return jpeg_buf;
}

static void eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
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
    if(info->captureCnt++ >= info->captureMaxCnt)
    {
        if(info->mode == 1)
        {
            info->captureCnt = 0;
            __LOG(LOG_NOTICE, "[%s][%s:%d] capture cnt reset", CAP_LOG_KEY, _FILE_, __LINE__);
        }
        else
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }
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
        __LOG(LOG_ERR, "[%s][%s:%d] buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    g_signal_emit_by_name(info->appsrc, "push-buffer", buffer, &ret);
    if (ret != GST_FLOW_OK)
    {
        g_printerr("Error pushing buffer to appsrc: %d\n", ret);
        return ret;
    }

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

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
            __LOG(LOG_NOTICE, "[%s][%s:%d] capture cnt reset", CAP_LOG_KEY, _FILE_, __LINE__);
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
        __LOG(LOG_ERR, "[%s][%s:%d] buffer cannot get from sample", CAP_LOG_KEY, _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    //GstBuffer *copied_buffer = gst_buffer_copy(buffer);
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        //g_printerr("Failed to map buffer\n");
        __LOG(LOG_ERR, "[%s][%s:%d] Failed to map buffer", CAP_LOG_KEY, _FILE_, __LINE__);
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

static GstFlowReturn on_new_sample_jpeg_to_file(GstElement *sink, gpointer userData) 
{
    GstSample *sample;
    CaptureData *info = (CaptureData *)userData;
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
            __LOG(LOG_NOTICE, "[%s][%s:%d] capture cnt reset", CAP_LOG_KEY, _FILE_, __LINE__);
        }
        else
        {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }
#endif

    long jpeg_size = 0;
    unsigned char *jpeg_buf = compress_frame_to_jpeg(sample, &jpeg_size, info);
    if (jpeg_buf && jpeg_size > 0) {
        extention = g_strdup(cmdArg.cap_encoder_en ? "jpg" : "rgb");

        path = g_strdup_printf("%s_%d.%s", info->filePath, info->captureCnt_++, extention);

        FILE *fp = fopen(path, "wb");
        if (fp) {
            fwrite(jpeg_buf, 1, jpeg_size, fp);
            fclose(fp);
            __LOG(LOG_INFO, "[%s][%s:%d] Saved JPEG to %s (%ld bytes)", CAP_LOG_KEY, _FILE_, __LINE__, path, jpeg_size);
        }
        else {
            __LOG(LOG_ERR, "[%s][%s:%d] Failed to open file %s for writing", CAP_LOG_KEY, _FILE_, __LINE__, path);
        }
        tjFree(jpeg_buf);
        g_free(path);
    }
    else __LOG(LOG_ERR, "[%s][%s:%d] Failed jpeg_size : %ld", CAP_LOG_KEY, _FILE_, __LINE__, jpeg_size);

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

void CaptureBin::setAppsrc(GstElement *appsrc)
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s ch:%d", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch);

    if(appsrc == NULL) __LOG(LOG_ERR, "[%s][%s:%d] %s ch:%d appsrc is NULL!", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch);

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
    captureData.captureCnt = cmdArg.captureMaxCnt;
    captureData.captureCnt_ = cmdArg.captureMaxCnt;
    captureData.captureMaxCnt = cmdArg.captureMaxCnt;
    captureData.mode = 0;
    captureData.debug = FALSE;
    captureData.tjCompressor = NULL;
}

CaptureBin::~CaptureBin()
{
    __LOG(LOG_INFO, "[%s][%s:%d] %s[%d]", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch);
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
    __LOG(LOG_INFO, "[%s][%s:%d] %s cnt:%d, maxCnt:%d", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.captureCnt, captureData.captureMaxCnt);

    return 1;
}

gint CaptureBin::stopCapture()
{
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__);
    captureData.captureCnt = cmdArg.captureMaxCnt;
    captureData.captureCnt_ = cmdArg.captureMaxCnt;
    captureData.mode = 0;

    return 1;
}

void CaptureBin::setMode(guint8 mode)
{
    __LOG(LOG_INFO, "[%s][%s:%d] ch%d setMode %d", CAP_LOG_KEY, _FILE_, __LINE__, mode);
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

gchar* CaptureBin::getCaptureFilePath()
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

        captureData.filePath = g_strdup_printf("%s/%s/%s_%s-ch%d", cmdArg.mntDir, cmdArg.captureDir, cmdArg.ohtName, date_str, captureData.ch);

        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);

        g_date_time_unref(datetime);
        g_free(date_str);
    }
    else
    {
        captureData.filePath = g_strdup_printf("%s/%s/%s-ch%d", cmdArg.mntDir, cmdArg.captureDir, prefix, captureData.ch);
        __LOG(LOG_DEBUG, "[%s][%s:%d] filePath : %s", CAP_LOG_KEY, _FILE_, __LINE__, captureData.filePath);
    }

    return 1;
}

gboolean CaptureBin::init(guint8 num, gboolean crop_en)
{
    gboolean ret = 0;
    GstPad *staticPad;
    GstCaps *caps;
    captureData.ch = num;
    captureData.fps = cmdArg.fps[STREAM_CAP][captureData.ch];
    //sinkPad = NULL;
    __LOG(LOG_NOTICE, "[%s][%s:%d] %s ch : %d, crop : %s", CAP_LOG_KEY, _FILE_, __LINE__, __FUNCTION__, captureData.ch, crop_en? "enable":"disable");

    be.bin = gst_bin_new(g_strdup_printf("captureBin%d", captureData.ch));
    be.queue = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", captureData.ch));
    be.queue2 = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d_2", captureData.ch));
    be.imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imx_convert%d", captureData.ch));
    be.convert = gst_element_factory_make("videoconvert", g_strdup_printf("convert%d", captureData.ch));
    //be.parse = gst_element_factory_make("h264parse", "h264parse");
    be.enc = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", captureData.ch));
    //be.enc = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    be.rate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", captureData.ch));
    be.sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", captureData.ch));
    be.crop = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", captureData.ch));
    be.overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", captureData.ch));
    be.capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", captureData.ch));
    be.capsfilter2 = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d_2", captureData.ch));
    be.queue_sink = gst_element_factory_make("appsink", g_strdup_printf("queue_sink%d", captureData.ch));
    be.queue_src = gst_element_factory_make("appsrc", g_strdup_printf("queue_src%d", captureData.ch));
    be.queue3 = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d_3", captureData.ch));

    //GstElement *imx_convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("imxconvert%d", captureData.ch));

    if (!be.bin || !be.queue || !be.enc || !be.rate || !be.sink || !be.imx_convert || !be.queue2 || !be.crop || !be.overlay || \
        !be.capsfilter || !be.convert || !be.queue_src || !be.queue_sink || !be.capsfilter2) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture element create error", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    gst_bin_add_many(GST_BIN(be.bin), be.queue, be.rate, be.imx_convert, be.enc, be.queue2, be.sink, be.crop, be.overlay, \
                    be.capsfilter, be.convert, be.capsfilter2, be.queue_sink, be.queue_src, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), be.queue_src, be.queue3, be.queue_sink, NULL);
    //gst_bin_add_many(GST_BIN(be.bin), imx_convert, , NULL);

#if 0
    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] capture bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }
#endif

#ifdef CHANNEL_EACH_CROPx
    if(cmdArg.cap_encoder_en)
    {
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.imx_convert, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
        else ret = gst_element_link_many(be.queue, be.capsfilter, be.enc, be.queue2, be.sink, NULL);
    }
    else
    {
        if(crop_en && cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.crop, be.overlay, be.imx_convert, be.capsfilter, be.queue2, be.sink, NULL);
        else if(cmdArg.overlay_en) ret = gst_element_link_many(be.queue, be.overlay, be.imx_convert, be.capsfilter, be.queue2, be.sink, NULL);
        else if(crop_en) ret = gst_element_link_many(be.queue, be.crop, be.imx_convert, be.capsfilter, be.queue2, be.sink, NULL);
        else ret = gst_element_link_many(be.queue, be.capsfilter, be.queue2, be.sink, NULL);
    }
#else
    //ret = gst_element_link_many(be.queue, be.crop, be.convert, be.enc, be.queue2, be.sink, NULL);
    ret = gst_element_link(be.queue, be.queue_sink);
    if (!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture queue_sink link err", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    if(cmdArg.turbojpeg)
        ret = gst_element_link_many(be.queue_src, be.crop, be.imx_convert, be.capsfilter, be.queue2, be.sink, NULL);
    else
        ret = gst_element_link_many(be.queue_src, be.crop, be.imx_convert, be.capsfilter, be.convert, be.capsfilter2, be.queue2, be.enc, be.sink, NULL);

#endif
    if (!ret) {
        __LOG(LOG_CRIT, "[%s][%s:%d] capture queue_sink link err", CAP_LOG_KEY, _FILE_, __LINE__);
        return ret;
    }

    g_object_set(be.queue_src, "is-live", TRUE, NULL);
    g_object_set(be.queue_src, "format", GST_FORMAT_TIME, NULL);
    captureData.appsrc = be.queue_src;

#if 1
    GstCaps *srccaps = gst_caps_new_simple("video/x-raw",
                                "format", G_TYPE_STRING, "RGBx",
                                "width", G_TYPE_INT, cmdArg.width*2,
                                "height", G_TYPE_INT, cmdArg.height,
                                //"framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                NULL);
    g_object_set(be.queue_src, "caps", srccaps, NULL);
    gst_caps_unref(srccaps);
#endif

#if 0
    queue_src_pad = gst_element_get_static_pad(be.queue3, "src");
    gst_pad_add_probe(queue_src_pad, GST_PAD_PROBE_TYPE_BUFFER, queue_probe_callback, &captureData, NULL);
    gst_object_unref(queue_src_pad);
#endif
    g_object_set(be.queue_sink, "drop", TRUE, NULL);
    g_object_set(be.queue_sink, "emit-signals", TRUE, "sync", TRUE, "async", FALSE, NULL);
    //g_signal_connect(be.queue_sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(be.queue_sink, "new-sample", G_CALLBACK(on_new_sample_from_sink), &captureData);
    //g_signal_connect(be.queue_sink, "new-preroll", G_CALLBACK(new_preroll_handler), NULL );

    caps = gst_caps_new_simple("video/x-raw",
                                "format", G_TYPE_STRING, "RGBx",
                                "width", G_TYPE_INT, cmdArg.width,
                                "height", G_TYPE_INT, cmdArg.height,
                                //"framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    caps = gst_caps_new_simple("video/x-raw",
                                "format", G_TYPE_STRING, "I420",
                                "width", G_TYPE_INT, cmdArg.width,
                                "height", G_TYPE_INT, cmdArg.height,
                                //"framerate", GST_TYPE_FRACTION, captureData.fps, 1,
                                NULL);

    g_object_set(be.capsfilter2, "caps", caps, NULL);
    gst_caps_unref(caps);

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
    
    g_object_set(be.queue, "max-size-time", GST_SECOND, "max-size-buffers", captureData.fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.queue2, "max-size-time", GST_SECOND, "max-size-buffers", captureData.fps, "leaky", LEAKY_DOWNSTREAM, NULL);
    //g_object_set(re.capsfilter, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
    //g_object_set(be.sink, "max-buffers", captureData.fps, NULL);
    //g_object_set(pipe->sink, "max-lateness", 1*GST_SECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    //g_object_set(be.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(be.sink, "async", TRUE, NULL);
    g_object_set(be.sink, "drop", TRUE, NULL);
    g_object_set(be.sink, "emit-signals", TRUE, "sync", TRUE, "async", FALSE, NULL);
    g_signal_connect(be.sink, "eos", G_CALLBACK(eos_callback), NULL);
    if(cmdArg.turbojpeg)
    {
        captureData.tjCompressor = tjInitCompress();
        g_signal_connect(be.sink, "new-sample", G_CALLBACK(on_new_sample_jpeg_to_file), &captureData);
    }
    else
        g_signal_connect(be.sink, "new-sample", G_CALLBACK(on_new_sample_to_file), &captureData);

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

    return ret;
}