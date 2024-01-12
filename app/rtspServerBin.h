/*
 *
 * Cantops rtspServerBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _RTSPSERVER_H_
#define _RTSPSERVER_H_

#include "util.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>

#define DYNAMIC_CAPSx

gint rtspServerStart();
void rtspServerStop();

typedef struct _RtspServerData
{
    GstElement *appsrc;
    gchar *appSrcName;
    guint8 start_f;
    GstBuffer *buf;
    GstCaps *caps;
    gboolean debug;
    guint8 ch;
} RtspServerData;

typedef struct _RtspServerElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *convert2;
    GstElement *capsfilter;
    GstElement *parse;
    GstElement *bin;
    GstElement *sink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *compositor;
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
    gboolean getStartFlag();
    void getBitrate();
    void setBitrate(guint16 data);
    void getFps();
    void setFps(guint16 data);
    void getCaps();
    void setOverlayText(gchar *text);
    void setRotation(guint16 data);
    void getRotation();
    void setTimeStampDebug();
    void setGop(guint16 data);
    void getGop();
    void getKeyframe();
    void setkeyframe(guint16 data);

private :
	
public :
	gboolean m_flagDestroy;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    guint8 ch;
    RtspServerElement re;
    GstPad *sinkPad;
    RtspServerData rtspServerData;
};

#endif