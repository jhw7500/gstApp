/*
 *
 * Cantops util.cpp support
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

#ifndef _UTIL_H_
#define _UTIL_H_
#include <syslog.h>
//#include <cstdio>
#include "global.h"

void mylog( int opt, const char* _szfmt, ... );
gint cmd_parser(int *argc, char **argv[]);
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define PROGRAM_NAME	"gstApp"

//extern void attachInterruptHandlers();

#endif