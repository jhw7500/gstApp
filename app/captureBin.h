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
#include <gst/app/gstappsink.h>

typedef struct _CaptureData
{
    guint8 ch;
    guint16 captureCnt;
    guint16 captureMaxCnt;
    GstBuffer *buf;
    gchar *filePath;
    guint8 mode;
    gint fps;
} CaptureData;

typedef struct _CaptureElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
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
	gint init(guint8 num, gboolean crop_en);
	gint destroy() ;
    gint setFilePath();
    gint startCapture(guint8 mode);
    gint stopCapture();
    GstPad* getBinSinkPad();
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	CaptureElement be;

private :
    GstPad *sinkPad;
    CaptureData captureData;
};

#endif