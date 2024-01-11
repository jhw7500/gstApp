/*
 *
 * Cantops muxBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _MUXBIN_H_
#define _MUXBIN_H_

#include "util.h"

typedef struct _MuxElement
{
    GstElement *sink[4];
    GstElement *bin;
} MuxElement;

typedef struct _SinkData
{
    guint8 ch;
    guint8 start_f;
} SinkData;

class MuxBin
{
public :
	static MuxBin* getInstance();
	gint init() ;
	gint destroy() ;
    GstPad* getBinPad();
    gint addSink();
    gint addSink(guint8 cnt);
    GstPad* getBinVideoSinkPad(guint8 ch);
    GstPad* getBinAudioSinkPad(guint8 ch);
    gint addPadVideoSink(guint8 ch);
    gint addPadAudioSink(guint8 ch);
    gboolean splitNow(gpointer data);

private :
    static gchararray format_location(GstElement *sink, guint arg0, gpointer data);
    
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    MuxElement me;
	SinkData sinkData[MAX_CHANNEL];
private :
    guint8 ch_enable_bit;
    
};

#endif