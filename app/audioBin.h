/*
 *
 * Cantops audioBin.cpp support
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

#ifndef _AUDIOBIN_H_
#define _AUDIOBIN_H_

#include "util.h"

typedef struct _AudioElement
{
    GstElement *element[10];
    GstElement *bin;
} AudioElement;

class AudioBin
{
public :
	static AudioBin* getInstance();
	gint init() ;
	gint destroy() ;
    gint addElement(const gchar* firstStr, ...);
    gint linkElement();
    GstPad* getBinSrcPad(guint8 ch);
    gint addBinSrcPad(guint8 ch);

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    AudioElement ae;
	
private :
    //AudioElement ae;
    guint8 ptr;
};

#endif