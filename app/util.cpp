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

#include "util.h"

guint8 log_level = 7;
guint8 dbg_level = 7;

void mylog( int opt, const char* _szfmt, ... )
{
	va_list va;
	char strTmp[512];
	const char *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};

	va_start( va, _szfmt );
	
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
        GDateTime *datetime = g_date_time_new_now_local();
        //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");

        //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
        //memcpy(file_name, tmp, 128);
 
		const char *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}

gint cmd_parser(int *argc, char **argv[])
{
    gint debug_level = 0;
    gchar *saveDot = "/tmp";
    gchar *saveDir = "/mnt/sd_cam";
    gint ch_enable = 0xff;
    gint resolution_mode = 0;
    gint rec_fps = 15;
    gint rtsp_fps = 15;
    gint rec_bitrate = 4096;
    gint rtsp_bitrate = 1024;
    GOptionContext *ctx;
    GError *err = NULL;
    GOptionEntry entries[] = {
        {"debug", 'd', 0, G_OPTION_ARG_INT, &debug_level, "do not output status information", "LEVEL"},
        {"dot", 's', 0, G_OPTION_ARG_STRING, &saveDot, "save dot representation of pipeline to FILE and exit", "FILE"},
        {"channel", 'c', 0, G_OPTION_ARG_INT, &ch_enable, "camera channel enable bit", "BIT"},
        {"output", 'o', 0, G_OPTION_ARG_STRING, &saveDir, "save video & audio file to directory", "DIRECTORY"},
        {"mode", 'm', 0, G_OPTION_ARG_INT, &resolution_mode, "resolution select FHD(0) and HD(1)", "INT"},
        {"frecord", NULL, 0, G_OPTION_ARG_INT, &rec_fps, "record frame per second", "INT"},
        {"frtsp", NULL, 0, G_OPTION_ARG_INT, &rtsp_fps, "rtsp frame per second", "INT"},
        {"brecord", NULL, 0, G_OPTION_ARG_INT, &rec_bitrate, "record bit per second", "INT"},
        {"brtsp", NULL, 0, G_OPTION_ARG_INT, &rtsp_bitrate, "rtsp bit per second", "INT"},
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    if(!g_option_context_parse(ctx, argc, argv, &err))
    {
        printf("Failed to initialize: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    printf("debug_level : %d\n", debug_level);
    printf("saveDot : %s\n", saveDot);
    printf("saveDirectory : %s\n", saveDir);
    printf("ch_enable : 0x%02x\n", ch_enable);
    printf("record fps : %d\n", rec_fps);
    printf("rtsp fps : %d\n", rtsp_fps);
    printf("record bitrate : %d\n", rec_bitrate);
    printf("rtsp bitrate : %d\n", rtsp_bitrate);
}