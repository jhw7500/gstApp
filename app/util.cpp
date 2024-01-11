/*
 *
 * Cantops util.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "util.h"

GstElement *pipeline = NULL;
GMainLoop *loop = NULL;
volatile sig_atomic_t is_interrupted = 0;
gboolean is_live = FALSE;
CmdArg cmdArg;
const char *test0;
const char *test1;
const char *test2;
void log_once(gint opt, const gchar *message) 
{
    static gchar lastMessage[256][256];
    static guint8 ptr = 0;

    for(guint8 i=0; i<ptr; i++)
    {
        if (strcmp(message, lastMessage[i]) == 0) {
            return;
        }
    }

    if(opt <= cmdArg.log_level || opt <= LOG_ALERT)
    {
        syslog( opt|LOG_LOCAL0, "%s", message);
    }
    if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT)
    {
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");
        const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};

		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);
        g_print("%s\n", message);
        g_date_time_unref(datetime);
        g_free(date_str);
    }

    //strncpy(lastMessage[ptr], message, sizeof(lastMessage));
    strncpy(lastMessage[ptr], message, sizeof(lastMessage[ptr]));
    ptr++;
}

void mylog(gint opt, const gchar* _szfmt, ... )
{
	va_list va;
	gchar strTmp[512];

	va_start( va, _szfmt );
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= cmdArg.log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
        GDateTime *datetime = g_date_time_new_now_local();
        //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");

        //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
        //memcpy(file_name, tmp, 128);
        const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}

guint charArrayToInt(gchar *arr)
{
    guint8 i;
    guint result = 0;

    for(i=0; (arr[i]!='\n' && arr[i]!='\r' && arr[i]!='\0' && arr[i]!=' '); i++)
    {
        //g_print("arr[%d] : %c %d\n", i, arr[i], arr[i]);
        result = (result * 10) + (arr[i] - '0');
    }

    //g_print("result : %d\n", result);

    return result;
}

gboolean compareBuf(guint8 *cmp1, guint8 *cmp2, guint8 len)
{
	guint8 i;

	for(i=0;i<len;i++)
	{
		if(cmp1[i] != cmp2[i])
			return 0;
	}
	return 1;
}

GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
    GstElement *data = (GstElement *)user_data;
    GstClockTime timestamp = GST_BUFFER_PTS(info->data);
    gchar *name;
    g_object_get(data, "name", &name, NULL);
    g_message("%s[%s]Timestamp: %" GST_TIME_FORMAT "\n", name, gst_pad_get_direction(pad)==1? "SRC":"SINK", GST_TIME_ARGS(timestamp));

    g_free(name);
    return GST_PAD_PROBE_OK;
}

gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    // 현재 시간 가져오기
    static GstClockTime prev_time = GST_CLOCK_TIME_NONE;
    GstClockTime current_time = GST_BUFFER_PTS(buffer);

    if (prev_time != GST_CLOCK_TIME_NONE) {
        // 이전 시간이 존재하면 현재 시간과의 차이 계산
        GstClockTimeDiff delay = current_time - prev_time;

        // 지연 출력
        g_print("Delay: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(delay));
    }

    // 이전 시간 업데이트
    prev_time = current_time;

    return TRUE;
}

gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix)
{
	FILE *fp;
	static gchar str[128];

    sprintf(str, "ls -ptr %s/%s*%s | grep -v '/$' | tail -1 | tr -d '\r\n'", path, prefix, suffix);
    fp = popen(str, "r");
    if (NULL == fp)
    {
        perror("popen() fail");
        __LOG(LOG_CRIT, "[CFG][%s:%d] popen fail", _FILE_, __LINE__);
    }
    while (fgets(str, 128, fp));
	__LOG(LOG_INFO, "[CFG][%s:%d] search_json_file : %s", _FILE_, __LINE__, str);
	//Eliminate(str, '\n');

	return str;
}