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
    guint8 ch;
} VideoData;

typedef struct _VideoElement
{
    GstElement *src;
    GstElement *teeCrop;
    GstElement *capsfilter;
    GstElement *convert;
    GstElement *rate;
    GstElement *convert2[MAX_CHANNEL/2];
    GstElement *crop[MAX_CHANNEL/2];
    GstElement *tee[MAX_CHANNEL/2];
    GstElement *overlay[MAX_CHANNEL/2];
    GstElement *queue[MAX_CHANNEL/2];
    GstElement *queue_main;
    GstElement *deinterlace;
    GstElement *bin;
} VideoElement;

class VideoBin
{
public :
	static VideoBin* getInstance();
    VideoBin();
    ~VideoBin();
	gboolean init(guint8 csiNum) ;
    gboolean addCrop(CropDir dir);
    gboolean addBinRtspSrcPad(guint8 ch);
    gboolean addBinRecordSrcPad(guint8 ch);
    gboolean addBinCaptureSrcPad(guint8 ch);
    GstPad* getBinRtspSrcPad(guint8 ch);
    GstPad* getBinRecordSrcPad(guint8 ch);
    GstPad* getBinCaptureSrcPad(guint8 ch);
    void getIoMode();
    void setIoMode(guint16 data);

private :
	
public :
	gboolean m_flagDestroy;
    VideoElement be;
    //GstElement *pipeline[2];
	
private :
    VideoData videoData;
    GstPad *srcRtspPad;
    GstPad *srcRecordPad;
    GstPad *srcCapturePad;
};

#endif