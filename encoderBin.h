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
    GstElement *capsfilter2;
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

/* 채널별 enc 큐 유입 누적 카운터를 읽는다.
 *
 * enc_q_in_probe 는 무데이터 감시용으로 항상 설치되므로(enc_stat_sec 와 무관)
 * 이 값은 상시 유효하다. health producer 가 소스 stall 을 판정하는 데 쓴다.
 * enc_out 계열은 enc_stat_sec > 0 일 때만 누적되므로 여기서 노출하지 않는다. */
guint64 encoderQueueInputTotal(guint8 ch);

class EncoderBin
{
public :
	static EncoderBin* getInstance() ;
    EncoderBin();
    ~EncoderBin();
	gboolean init(guint8 ch) ;
    GstPad* getBinSinkPad();
    void setBitrate(gint data);
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
    void forceKeyframe();
    void setDualBps(gboolean val);
    gboolean addBinRtspSrcPad();
    GstPad* getBinRtspSrcPad();
    gboolean addBinRecSrcPad();
    GstPad* getBinRecSrcPad();
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
    GstPad *sinkPad;
    GstPad *srcPad;
    EncData encData;
	GstPad *srcRtspPad;
    GstPad *srcRecPad;
};

#endif