/*
 *
 * Cantops testBin.cpp support
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

#include "testBin.h"

TestBin* TestBin::getInstance()
{
	static TestBin instance;
	return &instance;
}

TestBin::TestBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    ptr = 0;
    be.bin = NULL;
}

TestBin::~TestBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

GstPad* TestBin::getBinSrcPad(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);
    
    return gst_element_get_static_pad(be.bin, g_strdup_printf("test_src_ch%d", ch));;
}

gint TestBin::addBinSrcPad(guint8 ch)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch:%d ptr:%d", _FILE_, __LINE__, __FUNCTION__, ch, ptr);
    if(!gst_element_add_pad(be.bin, gst_ghost_pad_new(g_strdup_printf("test_src_ch%d", ch), gst_element_get_request_pad(be.element[ptr], "src_%u"))))
        g_error("error");
    
    return 0;
}

gint TestBin::linkElement()
{
    guint8 i = 0;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    for(i=0; i<ptr; i++)
    {
        if(!gst_element_link(be.element[i], be.element[i+1]))
            return 0;
    }

    return 1;
}

gint TestBin::addElement(const gchar* firstStr, ...)
{
    guint8 i = 0;
    va_list args;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    va_start(args, firstStr);
    const char* currentStr = firstStr;
    
    while (currentStr != NULL) {
        g_print("%s ", currentStr);
        be.element[i] = gst_element_factory_make(currentStr, g_strdup_printf("%s%d", currentStr, i));
        gst_bin_add(GST_BIN(be.bin), be.element[i]);
        //if(i!=0) gst_element_link(ae.element[i-1], ae.element[i]);
        currentStr = va_arg(args, const gchar*);
        i++;
    }

    ptr = i - 1;

    va_end(args);
    g_print("\n");

    return 0;
}

gint TestBin::init()
{
    gboolean ret = FALSE;
    
    if(be.bin) return ret;

    ptr = 0;
    be.bin = gst_bin_new("TestBin");
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if(!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Test bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    return ret;
}