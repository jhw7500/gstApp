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

#include "audioBin.h"

AudioBin* AudioBin::getInstance()
{
	static AudioBin instance;
	return &instance;
}

GstPad* AudioBin::getBinSrcPad(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    
    return gst_element_get_static_pad(ae.bin, g_strdup_printf("audio_src_ch%d", ch));;
}

gint AudioBin::addBinSrcPad(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d ptr:%d", _FILE_, __LINE__, __FUNCTION__, ch, ptr);
    if(!gst_element_add_pad(ae.bin, gst_ghost_pad_new(g_strdup_printf("audio_src_ch%d", ch), gst_element_get_request_pad(ae.element[ptr], "src_%u"))))
        g_error("error");
    
    return 0;
}

gint AudioBin::linkElement()
{
    guint8 i = 0;

    for(i=0; i<ptr; i++)
    {
        gst_element_link(ae.element[i], ae.element[i+1]);
    }

    return 0;
}

gint AudioBin::addElement(const gchar* firstStr, ...)
{
    guint8 i = 0;
    va_list args;
    va_start(args, firstStr);

    const char* currentStr = firstStr;
    while (currentStr != NULL) {
        g_print("%s ", currentStr);
        ae.element[i] = gst_element_factory_make(currentStr, g_strdup_printf("%s%d", currentStr, i));
        gst_bin_add(GST_BIN(ae.bin), ae.element[i]);
        //if(i!=0) gst_element_link(ae.element[i-1], ae.element[i]);
        currentStr = va_arg(args, const gchar*);
        i++;
    }

    ptr = i - 1;

    va_end(args);
    g_print("\n");

    return 0;
}

gint AudioBin::init()
{
    ptr = 0;
    ae.bin = gst_bin_new("audioBin");
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    if(!gst_bin_add(GST_BIN(pipeline), ae.bin))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] audio bin add error in pipeline", _FILE_, __LINE__);
        return -1;
    }

    return 0;
}