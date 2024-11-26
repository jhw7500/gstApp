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

typedef struct {
    void* arg0;
    void* arg1;
} CapArgs;

typedef struct _CaptureData
{
    GstElement *appsrc;
    guint8 ch;
    gint captureCnt;
    gint captureMaxCnt;
    GstBuffer *buf;
    gchar *filePath;
    guint8 mode;
    gint fps;
    gboolean debug;
} CaptureData;

typedef struct _CaptureElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *imx_convert;
    GstElement *convert;
    GstElement *parse;
    GstElement *bin;
    GstElement *sink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *capsfilter;
} CaptureElement;

class CaptureBin
{
public :
	static CaptureBin* getInstance();
    CaptureBin();
    ~CaptureBin();
	gboolean init(guint8 num, gboolean crop_en);
    gint setFilePath(guint8 *prefix);
    gint startCapture(gint maxCnt);
    gint stopCapture();
    gint getCaptureCnt();
    GstPad* getBinSinkPad();
    void setQueueSize(guint size);
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);
    void setTimeStampDebug();
    void setAppsrc(GstElement *appsrc);

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