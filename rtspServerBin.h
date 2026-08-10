/*
 *
 * Cantops rtspServerBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _RTSPSERVER_H_
#define _RTSPSERVER_H_

#include "util.h"

#define DYNAMIC_CAPSx
#define DEFAULT_RTSP_SESSION_CLEAN_PERIOD   30  //300

guint rtspServerStart();
void rtspServerStop();
void rtspServerCloseAllSessions();
void rtspServerSendEosToAllAppsrc();

typedef struct _RtspServerData
{
    GstElement *appsrc;
    gchar *appSrcName;
    guint8 start_f;
    GstBuffer *buf;
    GstCaps *caps;
    gboolean debug;
    guint8 ch;
    GstClockTime last_timestamp;
    gint64 last_log_us;
    gboolean dual_bps;
    gboolean mem_flags_logged;
    GMutex lock;    /* appsrc/caps 공유 보호 (streaming·명령·RTSP 서버 스레드) */
    gint appsrc_full;        /* appsrc 큐 상태: enough-data=1 / need-data=0 (원자적 접근) */
    gboolean wait_keyframe;  /* 드롭 후 키프레임까지 push 보류 (lock 보호) */
    gint64 last_enough_log_us;
    GstElement *kick_sink;   /* 강제 키프레임 이벤트 주입용 appsink (borrowed, init 시 1회 설정) */
} RtspServerData;

typedef struct _RtspServerElement
{
    GstElement *rate;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *convert2;
    GstElement *videoflip;
    GstElement *capsfilter;
    GstElement *capsfilter2;
    GstElement *parse;
    GstElement *bin;
    GstElement *sink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *compositor;
    GstElement *identity;
    GstElement *tee;
} RtspServerElement;

class RtspServerBin
{
public :
	static RtspServerBin* getInstance() ;
    RtspServerBin();
    ~RtspServerBin();
	gboolean init(guint8 num, gboolean crop_en) ;
    gboolean audioInit();
    GstPad* getBinSinkPad();
    gboolean getStartFlag();
    void getBitrate();
    void setBitrate(gint data);
    void getFps();
    void setFps(guint16 data);
    void getCaps();
    void setOverlayText(gchar *text);
    void setRotation(guint16 data);
    void getRotation();
    void setTimeStampDebug();
    void setGop(guint16 data);
    void getGop();
    void getKeyframe();
    void setkeyframe(guint16 data);
    void setDualBps(gboolean val);
    gboolean getDualBps();
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);
    gboolean addBinTeeRecordPad(guint8 ch);
    GstPad* getBinTeeRecordPad();

private :
	
public :
	gboolean m_flagDestroy;
    RtspServerElement re;
    //GstPad *sinkPad;
    //GstElement *pipeline[2];
	
private :
    GstPad *sinkPad;
    RtspServerData rtspServerData;
    GstPad *teeRecordPad;
};

#endif
