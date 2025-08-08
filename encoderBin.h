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

#ifndef _ENCODER_H_
#define _ENCODER_H_

#include "util.h"

typedef struct _EncElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *convert;
    GstElement *bin;
    GstElement *capsfilter;
    GstElement *crop;
    GstElement *overlay;
    GstElement *tee;
} EncElement;

typedef struct _EncData
{
    GstCaps *caps;
    gboolean debug;
    guint8 ch;
} EncData;

class EncoderBin
{
public :
	static EncoderBin* getInstance() ;
    EncoderBin();
    ~EncoderBin();
	gboolean init(guint8 num, gboolean crop_en) ;
    GstPad* getBinSinkPad();
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
    void setDualBps(gboolean val);
    gboolean addBinRtspSrcPad(guint8 ch);
    GstPad* getBinRtspSrcPad(guint8 ch);
    gboolean addBinRecSrcPad(guint8 ch);
    GstPad* getBinRecSrcPad(guint8 ch);
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    GstElement* getBinAppsrc();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);

private :

public :
	gboolean m_flagDestroy;
    EncElement re;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    guint8 ch;
    GstPad *sinkPad;
    GstPad *srcPad;
    EncData recordData;
	GstPad *srcRtspPad;
    GstPad *srcRecPad;
};

#endif