/*
 *
 * Cantops captureBin.cpp support
 *
 * Copyright (C)2022 Pointimage, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 *    This program is free software; you can redistribute  it and/or modify it
 *    under  the terms of  the GNU General  Public License as published by the
 *    Free Software Foundation;  either version 2 of the  License, or (at your
 *    option) any later version.
 */


#ifndef _CAPTRUE_H_
#define _CAPTURE_H_

#include "util.h"
#include <gst/app/gstappsink.h>

typedef struct _CaptureData
{
    guint8 ch;
    guint8 captureCnt;
    GstBuffer *buf;
    gchar *filePath;
} CaptureData;

typedef struct _CaptureElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *capsfilter;
    GstElement *parse;
    GstElement *bin;
    GstElement *sink;
    GstElement *crop;
} CaptureElement;

class CaptureBin
{
public :
	static CaptureBin* getInstance();
    CaptureBin();
    ~CaptureBin();
	gint init(guint8 num);
	gint destroy() ;
    gint setFilePath();
    gint startCapture();
    gint stopCapture();
    GstPad* getBinSinkPad();

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	
private :
    CaptureElement be;
    guint8 ch;
    GstPad *sinkPad;
    CaptureData captureData;
};

#endif