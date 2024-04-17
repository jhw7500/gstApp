/*
 *
 * Cantops recordBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _RECORD_H_
#define _RECORD_H_

#include "util.h"

typedef struct _RecordElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *parse;
    GstElement *bin;
    GstElement *bin2;
    GstElement *sink;
    GstElement *capsfilter;
    GstElement *crop;
    GstElement *overlay;
    GstElement *appsink;
    GstElement *appsrc;
} RecordElement;

typedef struct _RecordData
{
    GstElement *appsrc;
    gchar *appSrcName;
    guint8 start_f;
    GstBuffer *buf;
    GstCaps *caps;
    gboolean debug;
    guint8 ch;

} RecordData;

class RecordBin
{
public :
	static RecordBin* getInstance() ;
    RecordBin();
    ~RecordBin();
	gboolean init(guint8 num, gboolean crop_en) ;
    GstPad* getBinSinkPad();
    GstPad* getBinSrcPad();
    void setBitrate(guint16 data);
    void getBitrate();
    void setFps(guint16 data);
    void getFps();
    void setOverlayText(gchar *text);
    void setRotation(guint16 data);
    void getRotation();
    void setGop(guint16 data);
    void getGop();
    void getKeyframe();
    void setkeyframe(guint16 data);
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    GstElement* getBinAppsrc();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);

private :
	
public :
	gboolean m_flagDestroy;
    RecordElement re;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    guint8 ch;
    GstPad *sinkPad;
    GstPad *srcPad;
    RecordData recordData;
};

#endif