/*
 *
 * Cantops rtspServerBin.cpp support
 *
 * Copyright (C)2022 Cantops, Inc. All rights reserved.
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

#ifndef _RTSPSERVER_H_
#define _RTSPSERVER_H_

#include "util.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>

gint rtspStart();
void rtspStop();

typedef struct _RtspServerData
{
    GstElement *appsrc;
    gchar *appSrcName;
    guint8 ch;
    guint8 start_f;
    GstBuffer *buf;
    GstCaps *caps;
} RtspServerData;

typedef struct _RtspServerElement
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
    GstElement *overlay;
} RtspServerElement;

class RtspServerBin
{
public :
	static RtspServerBin* getInstance() ;
    RtspServerBin();
    ~RtspServerBin();
	gint init(guint8 num) ;
	gint destroy() ;
    GstPad* getBinSinkPad();
    gboolean getBitrate();
    gboolean setBitrate(guint16 data);
    gboolean getFps();
    gboolean setFps(guint16 data);
    gboolean getCaps();
    gboolean setOverlayText(gchar *text);
    gboolean getStartFlag();

private :
	
public :
	gboolean m_flagDestroy;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    RtspServerElement re;
    guint8 ch;
    GstPad *sinkPad;
    RtspServerData rtspServerData;
};

#endif