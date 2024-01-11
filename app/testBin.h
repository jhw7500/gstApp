/*
 *
 * Cantops testBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _TESTBIN_H_
#define _TESTBIN_H_

#include "util.h"

typedef struct _TestElement
{
    GstElement *element[10];
    GstElement *bin;
} TestElement;

class TestBin
{
public :
	static TestBin* getInstance();
	gint init() ;
    TestBin();
    TestBin(gboolean x);
    ~TestBin();
	gint destroy() ;
    gint addElement(const gchar* firstStr, ...);
    gint linkElement();
    GstPad* getBinSrcPad(guint8 ch);
    gint addBinSrcPad(guint8 ch);

private :
	
public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
    TestElement be;
	
private :
    //TestElement ae;
    guint8 ptr;
};

#endif