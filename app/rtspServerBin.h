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

#define DYNAMIC_CAPSx
#define DEFAULT_RTSP_SESSION_CLEAN_PERIOD   30  //300

guint rtspServerStart();
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
    GstClockTime last_timestamp;
} RtspServerData;

typedef struct _RtspServerElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *convert2;
    GstElement *videoflip;
    GstElement *capsfilter;
    GstElement *capsfilter2;
    GstElement *parse;
    GstElement *bin;
    GstElement *sink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *compositor;
    GstElement *identity;
} RtspServerElement;

class RtspServerBin
{
public :
	static RtspServerBin* getInstance() ;
    RtspServerBin();
    ~RtspServerBin();
	gboolean init(guint8 num, gboolean crop_en) ;
    gboolean audioInit();
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
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);

private :
	
public :
	gboolean m_flagDestroy;
    RtspServerElement re;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    GstPad *sinkPad;
    RtspServerData rtspServerData;
};

#endif