/*
 *
 * Cantops recordBin.cpp support
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

#ifndef _RECORD_H_
#define _RECORD_H_

#include "global.h"
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
} RecordElement;

class RecordBin
{
public :
	static RecordBin* getInstance() ;
    RecordBin();
    ~RecordBin();
	gint init(guint8 num) ;
	gint destroy() ;
    GstPad* getBinSinkPad();
    GstPad* getBinSrcPad();

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