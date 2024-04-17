/*
 *
 * Cantops main.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "util.h"
#include "videoBin.h"
#include "recordBin.h"
#include "testBin.h"
#include "muxSinkBin.h"
#include "rtspServerBin.h"
#include "captureBin.h"
#include "parser.h"
#include "aes.h"
#include "tcpServer.h"
#include <fcntl.h>
#include <unistd.h>
//#include <signal.h>

#define APP_VERSION "0.5"

#define SEGFAULT_DEBUG
#define RECORDBIN_ENABLE
#define RTSPSERVERBIN_ENABLE
#define AUDIOBIN_ENABLE
#define SPLIT_TIME_RECOVERY
//MuxSinkBin muxSinkBin[MAX_CHANNEL];

gboolean bus_message_parse(GstBus *bus, GstMessage *message, gpointer data)
{
    //PipeMain *info = (PipeMain *)data;
    static guint8 cam_cnt = 0;
    static GstState state = GST_STATE_VOID_PENDING;
    GstMessageType mType = GST_MESSAGE_TYPE(message);

    if(mType == GST_MESSAGE_QOS) return TRUE;

    if(mType == GST_MESSAGE_TAG) return TRUE;

    //if(mType == GST_MESSAGE_ELEMENT) return TRUE;

    if(mType == GST_MESSAGE_STREAM_STATUS) return TRUE;
    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    //if(mType == GST_MESSAGE_STATE_CHANGED) return TRUE;

    //__LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));

    switch(mType) 
    {
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            if(state != old_state)
            {
                //__LOG(LOG_NOTICE, "[GST][%s:%d] from %s to %s in %s", _FILE_, __LINE__, gst_element_state_get_name(old_state), gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
                //g_print("from %s to %s at %s\n", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
                state = old_state;
            }
            __LOG(LOG_INFO, "[GST][%s:%d] from %s to %s int %s", _FILE_, __LINE__, gst_element_state_get_name(old_state), gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
            break;
        }

        case GST_MESSAGE_ERROR:
        {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            __LOG(LOG_ERR, "[GST][%s:%d] error message : %s\n", __FILE__, __LINE__, err->message);
            __LOG(LOG_ERR, "[GST][%s:%d] error debug : %s\n", __FILE__, __LINE__, (debug)? debug : "none");
            printf("Error : %s\n", err->message);
            printf("Debug : %s\n", (debug)? debug : "none");
            g_error_free(err);
            g_free(debug);
            //destroy();
            gst_element_send_event(pipeline, gst_event_new_eos());
            break;
        }

        case GST_MESSAGE_EOS:
        {
            //printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
            //__LOG(LOG_NOTICE, "[GST][%s:%d] GST_MESSAGE_EOS (index:%d sigflag:%d)", _FILE_, __LINE__, info->index, sigflag);
            if(is_interrupted)
            {
                cam_cnt++;
                //if(cam_cnt >= MAX_PIPELINE) destroy();
            }
            else
            {
                gst_element_set_state(pipeline, GST_STATE_READY);
                //gst_element_get_state(pipeline[info->index], NULL, NULL, GST_CLOCK_TIME_NONE);
                //gst_element_seek(pipeline[info->index], 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);
                //gst_element_set_state(pipeline[info->index], GST_STATE_NULL);
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                //change_file_datetime(NULL);
            }

            break;
        }

        case GST_MESSAGE_ELEMENT:
        {
            //__LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));
            //g_print("%s\n", gst_structure_to_string(gst_message_get_structure(message)));
 			const GstStructure *structure = gst_message_get_structure(message);
			if(gst_structure_has_name(structure, "splitmuxsink-fragment-opened"))
            {
                __LOG(LOG_NOTICE, "[GST][%s:%d] filename : %s, time : %llu", __FILE__, __LINE__, \
                    g_value_get_string(gst_structure_get_value(structure, "location")), g_value_get_uint64(gst_structure_get_value(structure, "running-time")));
            }
            else
                __LOG(LOG_INFO, "[GST][%s:%d] %s", __FILE__, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));

            break;
        }

        case GST_MESSAGE_STREAM_STATUS:
        {
            GstStreamStatusType type;
            GstElement *owner;
            const GValue *val;
            gchar *path;
            GstTask *task = NULL;

            g_message ("received STREAM_STATUS");
            gst_message_parse_stream_status (message, &type, &owner);

            val = gst_message_get_stream_status_object (message);

            g_message ("type:   %d", type);
            path = gst_object_get_path_string (GST_MESSAGE_SRC (message));
            g_message ("source: %s", path);
            g_free (path);
            path = gst_object_get_path_string (GST_OBJECT (owner));
            g_message ("owner:  %s", path);
            g_free (path);
            g_message ("object: type %s, value %p", G_VALUE_TYPE_NAME (val),
            g_value_get_object (val));

            /* see if we know how to deal with this object */
            if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
                task = (GstTask *)g_value_get_object (val);
            }

            switch (type) {
                case GST_STREAM_STATUS_TYPE_CREATE:
                g_message ("created task %p", task);
                break;
                case GST_STREAM_STATUS_TYPE_ENTER:
                /* g_message ("raising task priority"); */
                /* setpriority (PRIO_PROCESS, 0, -10); */
                break;
                case GST_STREAM_STATUS_TYPE_LEAVE:
                break;
                default:
                break;
            }
            break;
        }

        case GST_MESSAGE_QOS:
        {
#if 1
            gboolean live;
            guint64 running_time, stream_time,timestamp,duration;
            gst_message_parse_qos (message,&live,&running_time,&stream_time,&timestamp,&duration);
            g_warning("GOt a QOS event %lu %lu %lu %lu", running_time, stream_time, timestamp, duration);
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %lu %f %d", jitter, prop, qual );

            GstFormat format;
            guint64 processed;
            guint64 dropped;
            gst_message_parse_qos_stats(message, &format, &processed, &dropped);

            g_print("QoS Message:\n");
            g_print("Format: %s\n", gst_format_get_name(format));
            g_print("Processed: %lu\n", processed);
            g_print("Dropped: %lu\n", dropped);
#endif
            break;
        }

        case GST_MESSAGE_TAG: 
        {
#if 1
            GstTagList *tags = NULL;
            gst_message_parse_tag (message, &tags);
            g_print ("Got tags from element %s\n", GST_OBJECT_NAME (message->src));
            gst_tag_list_foreach (tags, print_tag, NULL);
            gst_tag_list_free (tags);
            //handle_tags (tags);
            //gst_tag_list_unref (tags);
#endif
            break;
        }
#if 0
        case GST_MESSAGE_BUFFERING: 
        {
            gint percent = 0;

            /* If the stream is live, we do not care about buffering. */
            if (info->is_live) break;

            gst_message_parse_buffering (message, &percent);
            g_print ("Buffering (%3d%%)\r", percent);
            /* Wait until buffering is complete before start/resume playing */

            if (percent < 100)
                gst_element_set_state (pipeline, GST_STATE_PAUSED);
            else
                gst_element_set_state (pipeline, GST_STATE_PLAYING);
            break;

        }
#endif
        case GST_MESSAGE_CLOCK_LOST:
        {
            /* Get a new clock */
            g_print ("GST_MESSAGE_CLOCK_LOST\r");
            gst_element_set_state (pipeline, GST_STATE_PAUSED);
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
            break;
        }

        case GST_MESSAGE_LATENCY:
        {
                // when pipeline latency is changed, this msg is posted on the bus. we then have
                // to explicitly tell the pipeline to recalculate its latency
                // FIXME: this never works!
#if 1
                if (!gst_bin_recalculate_latency (GST_BIN(pipeline)))
                    g_print("Could not reconfigure latency.\n");
                else
                    g_print("Reconfigured latency.\n");
                break;
#endif

        }     

        case GST_MESSAGE_APPLICATION:
        {
            const GstStructure *s;
            s = gst_message_get_structure (message);
            if (gst_structure_has_name (s, "GstLaunchInterrupt")) {
            /* this application message is posted when we caught an interrupt and
            * we need to stop the pipeline. */
            g_print(("Interrupt: Stopping pipeline ...\n"));
            //res = ELR_INTERRUPT;
            //goto exit;
            }
            break;
        }

        case GST_MESSAGE_NEW_CLOCK:
        {
            gchar *str = g_strdup_printf("echo '%s' > %s &", g_date_time_format(g_date_time_new_now_local(), "%Y%m%d %H:%M:%S"), DEFAULT_START_VIDEO_TIME_PATH);
            if(system(str) < 0) 
                __LOG(LOG_ERR, "[GST][%s:%d] %s error in %s", _FILE_, __LINE__, str, __FUNCTION__);
            else
                __LOG(LOG_NOTICE, "[GST][%s:%d] %s in %s", _FILE_, __LINE__, str, __FUNCTION__);

            g_free(str);
#if 0
            FILE *fp = NULL;
            fp = fopen(DEFAULT_START_VIDEO_TIME_PATH, "wb");
            if(fp != NULL)
            {
                gchar *date_str = g_date_time_format(datetime, "%Y%m%d %H:%M:%S");
                fwrite(date_str, sizeof(char), strlen(date_str), fp);
                fclose(fp);
            }
#endif
            break;
        }
        
        default:
            __LOG(LOG_NOTICE, "[GST][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));
            break;

    }

    return TRUE;
}

static void check_terminal_input(gpointer arg)  //(gpointer arg0, gpointer arg1, gpointer arg2) 
{
    gint bytesRead;
    gchar buffer[64];

    gint flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    ParserClass* parser = ParserClass::getInstance();

    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);

    do
    {
        g_usleep(10000);

        if(is_interrupted)
            break;

        bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));

        if(bytesRead > 0)
        {
            //buffer[bytesRead] = '\0';
            //g_print("Input: %s", buffer);
            parser->cmd_parser(buffer, bytesRead, arg);
        }
        
    } while(1);

    __LOG(LOG_NOTICE, "[TERMINAL][%s:%d] %s break", _FILE_, __LINE__, __FUNCTION__);

    return;
}

static void splitCheck(gpointer data, guint8 startSec)
{
    MuxSinkBin* muxSinkBin = (MuxSinkBin *)data;
    static gboolean start_flag = 0;
    static gint target_min;
    guint8 i;
    GDateTime *datetime = g_date_time_new_now_local();
    gint sec = g_date_time_get_second(datetime);
    gint min = g_date_time_get_minute(datetime);

    //g_print("%s\n", __FUNCTION__);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    if(start_flag == 0)
    {
        for(i=0; i<MAX_CHANNEL; i++)
        {
            //if(!(cmdArg.ch_enable & (0x1 << i))) continue;
            if(!muxSinkBin[i].getBinVideoSinkPad()) continue;
            if(muxSinkBin[i].getStartFlag() == 0) { g_date_time_unref(datetime); return; }
        }

        if(sec != startSec) {
            target_min = g_date_time_get_minute(g_date_time_add_minutes(datetime, 1));
        }
        else target_min = min;
        //target_min = min + 1;
        //if(target_min >= 60) target_min = 0;
        __LOG(LOG_NOTICE, "[GST][%s:%d] next split time : %02dm %02ds", _FILE_, __LINE__, target_min, startSec);
        start_flag = 1;
    }

    if(target_min != min)
    {
        g_date_time_unref(datetime);
        return;
    }

    if(startSec != sec)
    {
        g_date_time_unref(datetime);
        return;
    }

#ifdef SPLIT_TIME_RECOVERY
    gint splitMax=0, splitMin=59999;

    for (i = 0; i < MAX_CHANNEL; i++)
    {
        if (muxSinkBin[i].getBinVideoSinkPad())
        {
            gint splitMsec = muxSinkBin[i].getSplitMsec();
            __LOG(LOG_INFO, "[GST][%s:%d] splitMsec[%d] : %d", _FILE_, __LINE__, i, splitMsec);
            if (splitMsec > splitMax) splitMax = splitMsec;
            if (splitMsec < splitMin) splitMin = splitMsec;

            muxSinkBin[i].setSplitMsec(DEFAULT_SPLIT_MAX_MSEC);
        }
    }
    __LOG(LOG_NOTICE, "[GST][%s:%d] splitMax : %d, splitMin : %d", _FILE_, __LINE__, splitMax, splitMin);

    if (splitMax - splitMin >= cmdArg.split_diff_msec || splitMax >= cmdArg.split_max_msec)
    {
        gchar *str = g_strdup_printf("echo '%s' > %s &", g_date_time_format(g_date_time_new_now_local(), "%Y%m%d %H:%M:%S"), DEFAULT_START_VIDEO_TIME_PATH);

        __LOG(LOG_ERR, "[GST][%s:%d] split time check error : splitMax : %d, splitMin : %d", _FILE_, __LINE__, splitMax, splitMin);

        if (system(str) < 0)
            __LOG(LOG_ERR, "[GST][%s:%d] %s error in %s", _FILE_, __LINE__, str, __FUNCTION__);
        else
            __LOG(LOG_NOTICE, "[GST][%s:%d] %s in %s", _FILE_, __LINE__, str, __FUNCTION__);

        g_free(str);

        __LOG(LOG_NOTICE, "[GST][%s:%d] split now", _FILE_, __LINE__);
        for (i = 0; i < MAX_CHANNEL; i++)
            if (muxSinkBin[i].getBinVideoSinkPad())
                muxSinkBin[i].splitNow(NULL, FALSE);
    }

#endif
    target_min = g_date_time_get_minute(g_date_time_add_minutes(datetime, cmdArg.duration));
    //target_min += cmdArg.duration;
    //if(target_min >= 60) target_min -= 60;
    __LOG(LOG_NOTICE, "[GST][%s:%d] next split time : %02dm %02ds", _FILE_, __LINE__, target_min, startSec);

    if(datetime) g_date_time_unref(datetime);

    return;
} 

static void splitLoop(gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);
    while(1)
    {
        if(is_interrupted)
            break;

        //if(splitCheck(data, 0))
            //break;
        splitCheck(data, cmdArg.split_sec);

        g_usleep(1000);
    }
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s break", _FILE_, __LINE__, __FUNCTION__);
}

static void taskLoop(gpointer arg)
{
    //splitCheck(data, 0);
    //splitTimerStart(data);
    //if(cmdArg.input_en) check_terminal_input(arg0, arg1, arg2);

    g_usleep(1000);
    
    return;
}

static gboolean setSRT(gpointer arg) 
{
    ThreadArgs *thraedArgs = (ThreadArgs *)arg;
    RecordBin *recordBin = (RecordBin *)(thraedArgs->arg1);
    RtspServerBin *rtspServerBin = (RtspServerBin *)(thraedArgs->arg2);
    //CaptureBin *captrueBin = (CaptureBin *)(thraedArgs->arg2);
    static gint index = 0;
    guint8 i;
    gchar* text;
    //GDateTime *datetime = g_date_time_new_now_local();
    //text = g_strdup_printf("2023-01-27 22:40:02 VD3001, M, A, 34049/174014(1000000), 1298.5678mm/s, 300mV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4");
#ifdef TIMEOVERLAY
    text = g_strdup_printf("VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4", index++);
#else
    text = g_strdup_printf("%s VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%%/71.5%%, E696, Level 7, Level 4", \
                        g_date_time_format(g_date_time_new_now_local(), "%Y-%m-%d %H:%M:%S"), index++);
#endif
    //__LOG(LOG_DEBUG, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_REC]) recordBin[i].setOverlayText(text);
        if(cmdArg.cam_en[i] && cmdArg.stream_en[STREAM_RTSP]) rtspServerBin[i].setOverlayText(text);
    }

    index++;

    g_free(text);

    return TRUE;
}

gint getPasswdWithAES(CmdArg *arg)
{
	gint ret = 0;
	gchar passwd[1024] = { 0, };
    const gchar *path = DEFAULT_PASSWD_PATH;
    AESClass *aesClass = AESClass::getInstance();
	//info->encrypt.id = strdup(DEFAULT_ENCRYPT_ID);

	if(aesClass->encrypt_get_passwd(path, passwd) < 0)
	{
		/* create */
		ret = aesClass->encrypt_change_passwd(path, NULL, DEFAULT_RTSP_PASSWD);
		if(ret < 0) {
			__LOG(LOG_ERR, "[CFG][%s:%d] Error change passwd .. ", _FILE_, __LINE__);
		}
		arg->rtsp_passwd = strdup(DEFAULT_RTSP_PASSWD);
	}
	else
		arg->rtsp_passwd = strdup(passwd);

	__LOG(LOG_NOTICE, "[CFG][%s:%d] id : %s, passwd : %s", _FILE_, __LINE__, arg->rtsp_id, arg->rtsp_passwd);

	return ret;
}

gint config_camera(guint8 i)
{
    gchar *cmd;
    guint16 ch_num0 = i*2;
    guint16 ch_num1 = i*2+1;

    if (cmdArg.cam_en[ch_num0] && cmdArg.cam_en[ch_num1])
    {
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d enable, ch%d enable", _FILE_, __LINE__, ch_num0, ch_num1);

        cmd = g_strdup_printf("i2cwrite %d 0x12 0x100c 0x%04x", i ? 1 : 2, (cmdArg.ch_rotate >> (i * 4 + 2)) & 0x03);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__, ch_num1);

        cmd = g_strdup_printf("i2cwrite %d 0x12 0x5002 %s", i ? 1 : 2, cmdArg.camConfig[ch_num1].ae_on? "0x0299":"0x0290");
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_on fail", __FILE__, __LINE__, ch_num1);

        cmd = g_strdup_printf("i2cwrite %d 0x12 0x5006 0x%04x", i ? 1 : 2, cmdArg.camConfig[ch_num1].ae_gain);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num1);
#if 1
        cmd = g_strdup_printf("i2cwrite %d 0x12 0x500c 0x%08x", i ? 1 : 2, cmdArg.camConfig[ch_num1].exp_time);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d exp_time fail", __FILE__, __LINE__, ch_num1);
#endif
        cmd = g_strdup_printf("i2cwrite %d 0x11 0x100c 0x%04x", i ? 1 : 2, (cmdArg.ch_rotate >> (i * 4)) & 0x03);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__, ch_num0);

        cmd = g_strdup_printf("i2cwrite %d 0x11 0x5002 %s", i ? 1 : 2, cmdArg.camConfig[ch_num0].ae_on? "0x0299":"0x0290");
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_on fail", __FILE__, __LINE__, ch_num0);

        cmd = g_strdup_printf("i2cwrite %d 0x11 0x5006 0x%04x", i ? 1 : 2, cmdArg.camConfig[ch_num0].ae_gain);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num0);
#if 1
        cmd = g_strdup_printf("i2cwrite %d 0x11 0x500c 0x%08x", i ? 1 : 2, cmdArg.camConfig[ch_num0].exp_time);
        __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
        if (system(cmd) < 0)
            __LOG(LOG_ERR, "[CFG][%s:%d] ch%d exp_time fail", __FILE__, __LINE__, ch_num0);
#endif
    }
    else
    {
#define STR_LEN 8
        FILE *fp;
        gchar str[STR_LEN];
        gchar val[4] = {0, 0, 0, 0};
        // memset(str, 0, STR_LEN);
        cmd = g_strdup_printf("i2cread %d 0x48 0x0013 1", i ? 1 : 2);
        fp = popen(cmd, "r");
        if (NULL == fp)
        {
            perror("popen() fail");
            return - 1;
        }
        while (fgets(str, STR_LEN, fp));
        pclose(fp);
        str[4] = 0;
        //__LOG(LOG_NOTICE, "[CFG][%s:%d] link byte : %s", _FILE_, __LINE__, str);

        if (strstr(str, "0xea"))
            val[ch_num0] = 1;
        else if (strstr(str, "0xda"))
            val[ch_num1] = 1;
        else
            __LOG(LOG_CRIT, "[CFG][%s:%d] csi[%d] not display", _FILE_, __LINE__, i);

        if (cmdArg.cam_en[ch_num0] & 0x01)
        {
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d enable, ch%d disable", _FILE_, __LINE__, ch_num0, ch_num1);
            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x100c 0x%04x", i ? 1 : 2, (cmdArg.ch_rotate >> (i * 4)) & 0x03);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__, ch_num0);
            if (val[ch_num0] == 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display", _FILE_, __LINE__, ch_num0, ch_num1);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x5002 %s", i ? 1 : 2, cmdArg.camConfig[ch_num0].ae_on? "0x0299":"0x0290");
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_on fail", __FILE__, __LINE__, ch_num0);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x5006 0x%04x", i ? 1 : 2, cmdArg.camConfig[ch_num0].ae_gain);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num0);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x500c 0x%08x", i ? 1 : 2, cmdArg.camConfig[ch_num0].exp_time);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num0, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num0);
        }
        else if (cmdArg.cam_en[ch_num1] & 0x01)
        {
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d disable, ch%d enable", _FILE_, __LINE__, ch_num0, ch_num1);
            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x100c 0x%04x", i ? 1 : 2, (cmdArg.ch_rotate >> (i * 4 + 2)) & 0x03);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] %s", _FILE_, __LINE__, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d rotation fail", __FILE__, __LINE__, ch_num1);
            if (val[ch_num1] == 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] swap : ch%d enable but ch%d display", _FILE_, __LINE__, ch_num1, ch_num0);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x5002 %s", i ? 1 : 2, cmdArg.camConfig[ch_num1].ae_on? "0x0299":"0x0290");
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_on fail", __FILE__, __LINE__, ch_num1);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x5006 0x%04x", i ? 1 : 2, cmdArg.camConfig[ch_num1].ae_gain);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num1);

            cmd = g_strdup_printf("i2cwrite %d 0x3c 0x500c 0x%08x", i ? 1 : 2, cmdArg.camConfig[ch_num1].exp_time);
            __LOG(LOG_NOTICE, "[CFG][%s:%d] ch%d cmd : %s", _FILE_, __LINE__, ch_num1, cmd);
            if (system(cmd) < 0)
                __LOG(LOG_ERR, "[CFG][%s:%d] ch%d ae_gain fail", __FILE__, __LINE__, ch_num1);
        }
    }

    g_free(cmd);

    return 1;
}

gint main(gint argc, gchar *argv[]) 
{
    guint major, minor, micro, nano;
    const gchar *nano_str;

    atexit(gst_deinit);
    gst_init(&argc, &argv);
    gst_version(&major, &minor, &micro, &nano);

    if(nano == 1)
        nano_str="(CVS)";
    else if(nano ==2)
        nano_str="(Prerelease)";
    else
        nano_str="";
    //printf("This program i linked against Gstreamer %d.%d.%d %s\n", major, minor, micro, nano_str);

    ParserClass* parser = ParserClass::getInstance();
    //cmdArg.appname = CHARNEXT(argv[0], '/');
    parser->init_arg(argv[0]);
    cmdArg = parser->arg;
    //getPasswdWithAES(&parser->arg);
    //attachInterruptHandlers();
    if(parser->json_parser(DEFAULT_JSON_PATH, DEFAULT_JSON_HEADER) < 0)
        return -1;

    if(parser->arg_parser(&argc, &argv) < 0)
        return -1;
    
    getPasswdWithAES(&parser->arg);
    //if(!strcmp(parser->arg.rtsp_passwd, DEFAULT_RTSP_PASSWD) == 0)
    

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s version : %s linked against Gstreamer %d.%d.%d %s", __FILE__, __LINE__, parser->arg.appname, APP_VERSION, major, minor, micro, nano_str);

    cmdArg = parser->arg;
    if(parser->check_arg() < 0)
        return -1;
    
    //MuxBin* muxBin = MuxBin::getInstance();
    GstBus *bus;
    VideoBin videoBin[MAX_VIDEO_SRC];
    RecordBin recordBin[MAX_CHANNEL];
    RtspServerBin rtspServerBin[MAX_CHANNEL];
    MuxSinkBin muxSinkBin[MAX_CHANNEL];
    CaptureBin captureBin[MAX_CHANNEL];
    TestBin audioBin;
    CTCPServer *tcpServer = CTCPServer::getInstance();
    GThread *splitThread = NULL, *terminalThread = NULL;
    GstStateChangeReturn ret;
    guint8 i;
    guint srtTimer_id = 0;
    ThreadArgs* thraedArgs = g_new(ThreadArgs, 1);
    const gchar *stateChangeReturnStr[4] = {"GST_STATE_CHANGE_FAILURE", "GST_STATE_CHANGE_SUCCESS", "GST_STATE_CHANGE_ASYNC", "GST_STATE_CHANGE_NO_PREROLL"};

    thraedArgs->arg0 = videoBin;
    thraedArgs->arg1 = recordBin;
    thraedArgs->arg2 = rtspServerBin;
    thraedArgs->arg3 = muxSinkBin;
    thraedArgs->arg4 = captureBin;

    pipeline = gst_pipeline_new(g_strdup_printf("%s_%s", cmdArg.appname, g_date_time_format(g_date_time_new_now_local(), "%Y%m%d_%H%M%S")));
    //pipeline2 = gst_pipeline_new(g_strdup_printf("%s2_%s", cmdArg.appname, g_date_time_format(g_date_time_new_now_local(), "%Y%m%d_%H%M%S")));
    //muxBin.init();
    //g_print("width : %d\n", cmdArg.res[cmdArg.resolution_mode].height);

    //signal_watch_intr_id = g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
    //signal_watch_hup_id = g_unix_signal_add (SIGHUP, (GSourceFunc) hup_handler, pipeline);
    addSignalHandler();

    if(!cmdArg.fault) fault_setup();

    g_setenv("GST_DEBUG_DUMP_DOT_DIR", cmdArg.dotDir, 1);
    //g_print("GST_DEBUG_DUMP_DOT_DIR : %s\n", g_getenv("GST_DEBUG_DUMP_DOT_DIR"));

    //gboolean crop_en[2] = {FALSE, FALSE};
    guint8 csiNum;
    cmdArg.crop_en[0] = cmdArg.cam_en[0]&&cmdArg.cam_en[1];
    cmdArg.crop_en[1] = cmdArg.cam_en[2]&&cmdArg.cam_en[3];

    for(i=0; i<MAX_CHANNEL; i++)
    {
        //if(!(cmdArg.ch_enable & (0x1 << i))) continue;
        if(!cmdArg.cam_en[i]) continue;
        //chNum = i;
        csiNum = (i/2);
        __LOG(LOG_INFO, "[GST][%s:%d] ch[%d] enable", _FILE_, __LINE__, i);
        if(!videoBin[csiNum].init(csiNum, cmdArg.crop_en[csiNum]))
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] csi%d video bin init err", _FILE_, __LINE__, csiNum);
            //goto main_end;
        }
//#if !defined(CHANNEL_EACH_CROP)
#ifndef CHANNEL_EACH_CROP
        videoBin[i/2].addCrop((CropDir)(i%2));
#endif
        if(cmdArg.stream_en[STREAM_CAP])
        {
            if(!videoBin[csiNum].addBinCaptureSrcPad(i))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d capture pad add err in csi%d video bin", _FILE_, __LINE__, i, csiNum);
                goto main_end;
            }

            //captureBin[i].setAppsrc(recordBin[chNum].getBinAppsrc());

            if(cmdArg.capture_always)
            {
                if(!captureBin[i].init(i, cmdArg.crop_en[csiNum]))
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] ch%d capture bin init err", _FILE_, __LINE__, i);
                    goto main_end;
                }

                if(!captureBin[i].addBinToPipe(pipeline))
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] ch%d captrue bin add err", _FILE_, __LINE__, i);
                    goto main_end;
                }

                if(gst_pad_link(videoBin[csiNum].getBinCaptureSrcPad(i), captureBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] ch%d capture pad link err", _FILE_, __LINE__, i);
                    //return -1;
                    goto main_end;
                }
            }
        }
        
        if(cmdArg.stream_en[STREAM_REC])
        {
            if(!recordBin[i].init(i, cmdArg.crop_en[csiNum]))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record init err", _FILE_, __LINE__, i, csiNum);
                goto main_end;
            }
#if 1
            if(!videoBin[csiNum].addBinRecordSrcPad(i))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record pad add err in csi%d video bin", _FILE_, __LINE__, i, csiNum);
                goto main_end;
            }

            if(gst_pad_link(videoBin[csiNum].getBinRecordSrcPad(i), recordBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
            {  
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record pad link err", _FILE_, __LINE__, i);
                goto main_end;
            }
#endif
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);
#if 1
            if(!muxSinkBin[i].init(i))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink init err", _FILE_, __LINE__, i);
                goto main_end;
            }
            
            if(!muxSinkBin[i].addBinVideoSinkPad())
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink pad add err", _FILE_, __LINE__, i);
                goto main_end;
            }
            
            if(gst_pad_link(recordBin[i].getBinSrcPad(), muxSinkBin[i].getBinVideoSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d record sink pad link err", _FILE_, __LINE__, i);
                goto main_end;
            }
#endif
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] mux video ch[%d] pad link", _FILE_, __LINE__, chNum);
            //g_thread_new("split-timer-thread", (GThreadFunc)splitTimerStart, &muxSinkBin[chNum]);
        }
        //json_parser(DEFAULT_JSON_PATH);
        //print_option();
        if(cmdArg.stream_en[STREAM_RTSP])
        {
            if(!rtspServerStart())
            {
                __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_, __LINE__);
                goto main_end;
            }

            if(!rtspServerBin[i].init(i, cmdArg.crop_en[csiNum]))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad link err", _FILE_, __LINE__, i);
                goto main_end;
            }

            if(!videoBin[csiNum].addBinRtspSrcPad(i))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad add err in csi%d video bin", _FILE_, __LINE__, i, csiNum);
                goto main_end;
            }
            
            if(gst_pad_link(videoBin[csiNum].getBinRtspSrcPad(i), rtspServerBin[i].getBinSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d rtsp pad link err", _FILE_, __LINE__, i);
                goto main_end;
            }
            //else __LOG(LOG_NOTICE, "[GST][%s:%d] Record ch[%d] pad link", _FILE_, __LINE__, chNum);
        }

        if(cmdArg.audio_en)
        {
            if(audioBin.init())
            {
                audioBin.addElement("alsasrc", "audioconvert", "audiorate", "lamemp3enc", "mpegaudioparse", "queue", "tee", NULL);
                if(!audioBin.linkElement()) {
                    __LOG(LOG_CRIT, "[GST][%s:%d] audio element link err", _FILE_, __LINE__);
                    goto main_end;
                }
                g_object_set(audioBin.be.element[0], "device", "plughw:0,0", NULL);
                //g_object_set(audioBin.be.element[0], "is-live", TRUE, NULL);
                //g_object_set(audioBin.be.element[0], "wave", 5, NULL);
                //g_object_set(audioBin.be.element[0], "wave", 8, NULL);
                //g_object_set(audioBin.be.element[0], "tick-interval", 200000000, NULL);
                //g_object_set(audioBin.be.element[2], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
                g_object_set(audioBin.be.element[5], "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 2, NULL);
                //g_object_set(audioBin.be.element[6], "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", LEAKY_DOWNSTREAM, NULL);
            }

            if(!audioBin.addBinSrcPad(i))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio pad add err", _FILE_, __LINE__, i);
                goto main_end;
            }

            if(!muxSinkBin[i].addBinAudioSinkPad())
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio sink pad add err", _FILE_, __LINE__, i);
                goto main_end;
            }

            if(gst_pad_link(audioBin.getBinSrcPad(i), muxSinkBin[i].getBinAudioSinkPad()) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] ch%d audio pad link err", _FILE_, __LINE__, i);
                //return -1;
                goto main_end;
            }
            else __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d audio pad link", _FILE_, __LINE__, i);
        }
    }

    //gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(pipeline));
    //GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_VERBOSE, gst_element_get_name(pipeline));

    bus = gst_element_get_bus(pipeline);
    if (!bus)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
        goto main_end;
    }

    gst_bus_add_watch(bus, bus_message_parse, NULL);

    gst_object_unref(bus);

    ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] paused : %s", _FILE_, __LINE__, stateChangeReturnStr[ret]);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state paused error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        goto main_end;
    }
    else if(ret == GST_STATE_CHANGE_NO_PREROLL)
    {
        is_live = TRUE;
        //__LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state paused", _FILE_, __LINE__);
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] delay %d sec for play", __FILE__, __LINE__, cmdArg.play_delay);
    sleep(cmdArg.play_delay);

	for(i = 0; i < MAX_VIDEO_SRC; ++i)
	{
		if(videoBin[i].be.bin != NULL)
		{
            config_camera(i);
        }
    }

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    __LOG(LOG_NOTICE, "[GST][%s:%d] playing : %s", _FILE_, __LINE__, stateChangeReturnStr[ret]);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state playing error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        goto main_end;
    }
#if 0
    ret = gst_element_set_state(pipeline2, GST_STATE_PLAYING);
    __LOG(LOG_NOTICE, "[GST][%s:%d] playing : %s", _FILE_, __LINE__, stateChangeReturnStr[ret]);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline2 state playing error", _FILE_, __LINE__);
        gst_object_unref(pipeline2);
        goto main_end;
    }
#endif
    if(cmdArg.input_en) {
        terminalThread = g_thread_new("terminal-thread", (GThreadFunc)check_terminal_input, thraedArgs);
    }

    if(cmdArg.stream_en[STREAM_REC] || cmdArg.audio_en) {
        splitThread = g_thread_new("split-thread", (GThreadFunc)splitLoop, muxSinkBin);
    }
    
    if(cmdArg.overlay_en) {
        srtTimer_id = g_timeout_add(500, (GSourceFunc)setSRT, thraedArgs);
    }

    if(cmdArg.tcp_en) {
        tcpServer->init(thraedArgs);
    }

    loop = g_main_loop_new(NULL, FALSE);

	if(!loop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] mainLoop create error", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] mainLoop start", _FILE_, __LINE__);
#if 0
        g_main_loop_run(loop);
#else
        while (!is_interrupted)
        {
            g_main_context_iteration(g_main_loop_get_context(loop), FALSE);
            taskLoop(NULL);
        }
#endif
    }
    //__LOG(LOG_NOTICE, "[GST][%s:%d] Main loop exit", _FILE_, __LINE__);

#if 0
    if(!gst_element_send_event(pipeline, gst_event_new_eos()))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to gst_element_send_event", _FILE_, __LINE__);
    }
#else
    for(i=0; i<MAX_CHANNEL; i++)
    {
        if(muxSinkBin[i].getBinVideoSinkPad()) gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(), gst_event_new_eos());
        if(muxSinkBin[i].getBinAudioSinkPad()) gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(), gst_event_new_eos());
        //if(rtspServerBin[i].getBinSinkPad()) gst_pad_send_event(rtspServerBin[i].getBinSinkPad(), gst_event_new_eos());
        //if(captureBin[i].getBinSinkPad()) gst_pad_send_event(captureBin[i].getBinSinkPad(), gst_event_new_eos());
    }
#endif

    //sleep(1);

main_end:
    __LOG(LOG_NOTICE, "[GST][%s:%d] main loop end", _FILE_, __LINE__);

    if(loop) g_main_loop_unref(loop);

    if(thraedArgs) g_free(thraedArgs);

    if(terminalThread) {
        g_thread_join(terminalThread);
        g_thread_unref(terminalThread);
    }
    if(splitThread) {
        g_thread_join(splitThread);
        g_thread_unref(splitThread);
    }
    if(srtTimer_id) {
        g_source_remove(srtTimer_id);
    }

    rtspServerStop();
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state set NULL...", _FILE_, __LINE__);

    if (gst_element_set_state (pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Failed to unset pipeline state", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Pipeline unset successfully", _FILE_, __LINE__);
    }

    if(pipeline) gst_object_unref(pipeline);

    if(cmdArg.tcp_en) {
        tcpServer->destroy();
    }

    removeSignalHandler();

    exit(0);
}
