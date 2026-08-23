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
    GstElement *mp4mux;
    GstElement *tsmux;
    GstElement *qtmux;
    GstElement *queue;
    GstElement *parse;
    GstElement *capsfilter;
} MuxSinkElement;

typedef struct _MuxSinkData
{
    guint8 ch;
    guint8 start_f;
    gint split_msec;
    gchar *last_timestamp;  // 마지막 녹화 타임스탬프 (예: "20260127_143000")
    GstClockTime last_running_time; // 이전 분할 시점의 running-time
    GstClockTime last_end_time;     // 마지막으로 닫힌 파일의 종료 시간
    GstClockTime last_duration;     // 마지막으로 닫힌 파일의 실제 녹화 길이
    /* 새 조각이 열린 시점의 running-time. 파이프라인 공통 시간축 위의 '내용' 시각이라
     * 채널 간 직접 비교가 가능하다. split_msec(벽시계)과 달리 콜백 처리 지연이 섞이지 않는다. */
    GstClockTime split_running_time;
    /* split_running_time 이 '지금 열린 조각' 의 값인지 표시한다.
     * format_location(스트리밍 스레드)이 split_msec 을 갱신할 때 1, fragment-opened 를
     * 처리하는 handleFragmentOpened(메인 루프)에서 0 이 된다.
     * 이 표식이 없으면 자동 롤오버 시 splitCheck 가 '새 조각의 running-time' 과
     * '다른 채널의 이전 조각 running-time' 을 섞어 비교해 분 단위 가짜 스큐를 만든다.
     * 64bit running-time 자체는 메인 루프만 만지고, 스레드 간 공유는 이 32bit 원자값으로 한정한다. */
    gint split_rt_stale;
} MuxSinkData;

void setSplitTargetEpoch(gint64 epoch_sec);
gint64 getSplitTargetEpoch();

class MuxSinkBin
{
public :
	static MuxSinkBin* getInstance();
	gboolean init(guint8 num) ;
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
    void setLastRunningTime(GstClockTime rt);
    GstClockTime getSplitRunningTime();
    void setSplitRunningTime(GstClockTime rt);
    gboolean isSplitRunningTimeFresh();
    void handle_last_sample();
    void handleFragmentOpened(const gchar *location, GstClockTime running_time);
    void handleFragmentClosed(const gchar *location, GstClockTime running_time, GstClockTime duration);
    GstPad* getBinQueuePad();
    //gchararray format_location(GstElement *sink, guint arg0, gpointer data);
private :
    //static gchararray format_location(GstElement *sink, guint arg0, gpointer data);
    
public :
    MuxSinkElement be;
    
private :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    MuxSinkData muxSinkData;
    GstPad *sinkAudioPad;
    GstPad *sinkAudioPad_;
    GstPad *sinkVideoPad;
    GstPad *sinkPad;
};

#endif
