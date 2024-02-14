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
    GstElement *sink;
    GstElement *capsfilter;
    GstElement *crop;
    GstElement *overlay;
} RecordElement;

class RecordBin
{
public :
	static RecordBin* getInstance() ;
    RecordBin();
    ~RecordBin();
	gint init(guint8 num, gboolean crop_en) ;
	gint destroy() ;
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

private :
	
public :
	gboolean m_flagDestroy;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    RecordElement re;
    guint8 ch;
    GstPad *sinkPad;
    GstPad *srcPad;
};

#endif