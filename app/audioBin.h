/*
 *
 * Cantops audioBin.cpp support
 *
 * Copyright (C)2023 cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */


#ifndef _AudioBIN_H_
#define _AudioBIN_H_

#include "util.h"

typedef struct _AuidoData
{
    guint8 ch;
} AudioData;

typedef struct _AudioElement
{
    GstElement *src;
    GstElement *rate;
    GstElement *resample;
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *convert;
    GstElement *parse;
    GstElement *bin;
    GstElement *capsfilter;
    GstElement *tee;
    GstElement *mux;
    GstElement *sink;
    GstElement *filter;
} AudioElement;

class AudioBin
{
public :
	static AudioBin* getInstance();
    AudioBin();
    ~AudioBin();
	gboolean init();
    GstPad* getBinSrcPad(guint8 ch);
    gboolean addBinSrcPad(guint8 ch);

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	AudioElement be;

private :
    AudioData audioData;
    GstPad *audioSrcPad;
};

#endif