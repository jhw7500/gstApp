/*
 *
 * Cantops captureBin.cpp support
 *
 * Copyright (C)2023 cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */


#ifndef _CAPTRUEBIN_H_
#define _CAPTUREBIN_H_

#include "util.h"
#include <turbojpeg.h>

#define CAP_LOG_KEY "CAP"
#define TURBO_JPEG

// Forward declaration for async capture task
typedef struct _AsyncCaptureTask AsyncCaptureTask;

typedef struct {
    void* arg0;
    void* arg1;
} CapArgs;

typedef enum
{
    CAP_ENC_JPEG = 0,
    CAP_ENC_TURBO =1,
    CAP_ENC_RAW =2
} CapEncType;

typedef struct _CaptureData
{
    GstElement *appsrc;
    guint8 ch;
    gint captureCnt;
    gint captureCnt_;
    gint captureMaxCnt;
    GstBuffer *buf;
    guint8 mode;
    gint fps;
    gboolean debug;
    tjhandle tjCompressor;
    CapEncType enc_type;
    const gchar *filePath;
    const gchar *extention;
    gint quality;
    // Async capture support
    GAsyncQueue *task_queue;
    GThread *worker_thread;
    volatile gboolean worker_running;
} CaptureData;

typedef struct _CaptureElement
{
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *imx_convert;
    GstElement *bin;
    GstElement *appsink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *capsfilter;
    GstElement *queue_sink;
    GstElement *appsrc;
} CaptureElement;

class CaptureBin
{
public :
	static CaptureBin* getInstance();
    CaptureBin();
    ~CaptureBin();
	gboolean init(guint8 num);
    gint setFilePath(guint8 *prefix);
    gint startCapture(gint maxCnt);
    gint stopCapture();
    void setMode(guint8 mode);
    guint8 getMode();
    guint8 getFPS();
    gint getCaptureCnt();
    gint getCaptureCnt_();
    const gchar* getCaptureFilePath();
    GstPad* getBinSinkPad();
    void setQueueSize(guint size);
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);
    void setTimeStampDebug();
    void setAppsrc(GstElement *appsrc);
    void setMaxCnt(guint16 maxCnt);
    void startWorker();
    void stopWorker();
    gboolean isQueueEmpty();

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	CaptureElement be;
    gboolean add_cap_f = 0;
    gulong probe_id;
    GstPad *queue_src_pad;
    //CaptureData captureData;

private :
    GstPad *sinkPad;
    CaptureData captureData;
    
};

#endif