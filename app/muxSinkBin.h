/*
 *
 * Cantops muxSinkBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _MUXSINKBIN_H_
#define _MUXSINKBIN_H_

#include "util.h"

typedef struct _MuxSinkElement
{
    GstElement *sink;
    GstElement *bin;
} MuxSinkElement;

typedef struct _MuxSinkData
{
    guint8 ch;
    guint8 start_f;
    gint split_msec;
} MuxSinkData;

class MuxSinkBin
{
public :
	static MuxSinkBin* getInstance();
	gint init(guint8 num) ;
	gint destroy() ;
    MuxSinkBin();
    ~MuxSinkBin();
    GstPad* getBinPad();
    GstPad* getBinVideoSinkPad();
    GstPad* getBinAudioSinkPad();
    gboolean splitNow(gpointer data, gboolean timer_en);
    gboolean addBinAudioSinkPad();
    gboolean addBinVideoSinkPad();
    guint8 getStartFlag();
    gint getSplitMsec();
    void setSplitMsec(gint msec);
    //gchararray format_location(GstElement *sink, guint arg0, gpointer data);
private :
    //static gchararray format_location(GstElement *sink, guint arg0, gpointer data);
    
public :

private :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    MuxSinkElement be;
    MuxSinkData muxSinkData;
    GstPad *sinkAudioPad;
    GstPad *sinkVideoPad;
};

#endif