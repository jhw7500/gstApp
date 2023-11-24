/*
 *
 * Cantops videoBin.cpp support
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


#ifndef _VIDEO_H_
#define _VIDEO_H_

#include "util.h"

typedef struct _VideoData
{
    guint8 csi;
} VideoData;

typedef struct _VideoElement
{
    GstElement *src;
    GstElement *teeCrop;
    GstElement *capsfilter;
    GstElement *convert;
    GstElement *convert2[MAX_CHANNEL/2];
    GstElement *crop[MAX_CHANNEL/2];
    GstElement *tee[MAX_CHANNEL/2];
    GstElement *overlay[MAX_CHANNEL/2];
    GstElement *queue[MAX_CHANNEL/2];
    GstElement *queue_main;
    GstElement *bin;
} VideoElement;

class VideoBin
{
public :
	static VideoBin* getInstance();
    VideoBin();
    ~VideoBin();
	gint init(CsiNum num) ;
	gint destroy() ;
    gint addCrop(CropDir dir);
    gint addBinRtspSrcPad(ChannelNum ch);
    gint addBinRecordSrcPad(ChannelNum ch);
    gint addBinCaptureSrcPad(ChannelNum ch);
    GstPad* getBinRtspSrcPad(ChannelNum ch);
    GstPad* getBinRecordSrcPad(ChannelNum ch);
    GstPad* getBinCaptureSrcPad(ChannelNum ch);
    void getIoMode();
    void setIoMode(guint16 data);

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	
private :
    VideoElement be;
    guint8 csi;
    GstPad *srcRtspPad;
    GstPad *srcRecordPad;
    GstPad *srcCapturePad;
};

#endif