/*
 *
 * Cantops videoBin.cpp support
 *
 * Copyright (C)2023 Pointimage, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
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
	gint init(CsiNum num, gboolean crop_en) ;
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
    VideoElement be;
    //GstElement *pipeline[2];
	
private :
    guint8 csi;
    GstPad *srcRtspPad;
    GstPad *srcRecordPad;
    GstPad *srcCapturePad;
};

#endif