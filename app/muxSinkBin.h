/*
 *
 * Cantops muxSinkBin.cpp support
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

#ifndef _MUXSINKBIN_H_
#define _MUXSINKBIN_H_

#include "global.h"
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
} MuxSinkData;

class MuxSinkBin
{
public :
	static MuxSinkBin* getInstance();
	gint init(guint8 num) ;
	gint destroy() ;
    GstPad* getBinPad();
    GstPad* getBinVideoSinkPad();
    GstPad* getBinAudioSinkPad();
    gboolean splitNow(gpointer data);
    gboolean addBinAudioSinkPad();
    gboolean addBinVideoSinkPad();
    guint8 getStartFlag();
    //gchararray format_location(GstElement *sink, guint arg0, gpointer data);
private :
    //static gchararray format_location(GstElement *sink, guint arg0, gpointer data);
    
public :

private :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    MuxSinkElement me;
    guint8 ch_enable_bit;
    MuxSinkData muxSinkData;
    GstPad *sinkAudioPad;
    GstPad *sinkVideoPad;
};

#endif