#include <gst/gst.h>
#include <stdio.h>
#include <syslog.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/check/gstcheck.h>
//gchar file_name[128];

#define SPLITSINK_ENABLEx
#define FILESINK_ENABLEx
#define FILENAME_SEC_ZEROx

#define FILE0_ENABLE
#define FILE1_ENABLEx
#define RTSP0_ENABLE
#define RTSP1_ENABLE
#define RTSP_TESTx

#define HIGH_BITRATE    4096
#define LOW_BITRATE     1024

#define APPSRC_NAME    "appsrc"
#define APPSRC2_NAME    "appsrc2"
#define APPSRC3_NAME    "appsrc3"

#define QUEUE_NAME    "queue"
#define QUEUE_RTSP_NAME    "queueRtsp"

void mylog( int opt, const char* _szfmt, ... );
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define _FILE_  strrchr(__FILE__,'/')? strrchr(__FILE__,'/')+1:__FILE__
#define PROGRAM_NAME	"timeApp"
#define FILE_SAVE_DURATION 30
#define _WIDTH   1920
#define _HEIGHT  1080
#define MAIN_FPS 30
#define FILE_FPS 30
#define RTSP_FPS 15

GstElement *pipeline;
GgstLoop *gstLoop;
//GgstLoop *rtspLoop2;
//GgstLoop *rtspLoop3;
//GThread *rtspThread2;
//GThread *rtspThread3;
//pthread_t m_threadRtsp2;
//pthread_t m_threadRtsp3;
GstElement *pipeline2;
GstRTSPServer *rtspServer;
GstRTSPMountPoints *rtspMounts;

unsigned char log_level = 7;
unsigned char dbg_level = 7;

typedef struct _CustomData{
    gchar* file_name;
    gchar* file_name_tmp;
    gchar* appsrc_name;
    guint8 ch;
    GstElement *appsrc;
    GstCaps *caps;
    GstElement *queue;
    //GgstLoop *rtspLoop;
    //GThread *rtspThread;
    //pthread_t m_threadRtsp;
    gboolean get_1st_frame;
    gboolean push_1st_iframe;
} CustomData;
//CustomData info[2];

typedef struct _CamPipe
{
    GstElement *crop;
    GstElement *tee;
    GstElement *sink[2];
    GstElement *encoder[2];
    GstElement *queue[2];
    GstElement *parse[2];
} CamPipe;

typedef struct _MainPipe
{
    GstElement *pipeline;
    GstElement *src;
    GstElement *tee;
    GstElement *capsfilter;
    GstElement *convert;
    GstCaps *caps;
    CamPipe cam[2];
} MainPipe;

void mylog( int opt, const char* _szfmt, ... )
{
	va_list va;
	char strTmp[512]; 
	time_t timer;
	struct tm t;
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

static void destroy(void)
{
    __LOG(LOG_EMERG, "[GST][%s:%d] destroy!!", _FILE_, __LINE__);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline); 
    g_main_loop_quit(gstLoop);
    //g_main_loop_quit(rtspLoop2);
    //g_main_loop_quit(rtspLoop3);
    //g_thread_join(rtspThread2);
    //g_thread_join(rtspThread3);
    //g_thread_unref(rtspThread2);
    //g_thread_unref(rtspThread3);
    g_object_unref(rtspServer);
    g_main_loop_unref(gstLoop);
    //g_main_loop_quit(info[0].rtspLoop);
    //g_main_loop_quit(info[1].rtspLoop);
    //g_thread_join(info[0].rtspThread);
    //g_thread_unref(info[1].rtspThread);
    //pthread_join(info[0].m_threadRtsp, NULL);
    //pthread_join(info[1].m_threadRtsp, NULL);

    exit(EXIT_FAILURE);

    return;
}

static void print_tag(const GstTagList * list, const gchar * tag, gpointer unused)
{
    gint i, count;

    count = gst_tag_list_get_tag_size (list, tag);

    for (i = 0; i < count; i++) {
      gchar *str;

      if (gst_tag_get_type (tag) == G_TYPE_STRING) {
        if (!gst_tag_list_get_string_index (list, tag, i, &str))
          g_assert_not_reached ();
      } else {
        str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
      }

      if (i == 0) {
        g_print ("  %15s: %s\n", gst_tag_get_nick (tag), str);
      } else {
        g_print ("                 : %s\n", str);
      }

      g_free (str);
    }
}

static gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));

    switch(GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            printf("[GST][%s:%d] Pipeline state changed from %s to %s\n", __FILE__, __LINE__, \
				gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
            break;
        }

        case GST_MESSAGE_ERROR:
        {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            printf("Error : %s\n", err->message);
            printf("Debug : %s\n", (debug)? debug : "none");
            g_error_free(err);
            g_free(debug);
            destroy();
            break;
        }

        case GST_MESSAGE_EOS:
        {
            destroy();
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
                task = g_value_get_object (val);
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
            gboolean live;
            guint64 running_time, stream_time,timestamp,duration;
            // gst_message_parse_qos (msg,&live,&running_time,&stream_time,&timestamp,&duration);
            // g_warning("GOt a QOS event %llu %llu %llu %llu", running_time, stream_time, timestamp, duration);
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %lld %f %lu", jitter, prop, qual );
            break;
        }

        case GST_MESSAGE_TAG: 
        {
#if 0
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

        default:
            break;
    }

    return TRUE;
}

static gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("output_%s-ch%d.mp4", date_str, info->ch);

    __LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

void muxer_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] muxer_added", _FILE_, __LINE__);

    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "sink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "sink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_error("sink name err");
    }

    return;
}

void sink_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] sink_added", _FILE_, __LINE__);

    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "sink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "sink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_error("sink name err");
    }

    return;
}
static gboolean testFunc(gpointer data)
{
    g_print("%s\n", __FUNCTION__);
    GstElement *pp = (GstElement *)data;
    gst_element_set_state(pp, GST_STATE_PAUSED);
}

static gboolean changeBitrate(gpointer data)
{
    GstElement *enc = (GstElement *)data;
    gint bitrate;
    g_object_get(enc, "bitrate", &bitrate, NULL);
    g_print("bitrate:%d\n", bitrate);

    if(bitrate == 4096) g_object_set(enc, "bitrate", 1024, NULL);
    if(bitrate == 1024) g_object_set(enc, "bitrate", 4096, NULL);
    
    return TRUE;
}

static gboolean changeFPS(gpointer data)
{
    GstElement *capsfilter = (GstElement *)data;
    GstCaps *caps;

    g_object_get(capsfilter, "caps", &caps, NULL);

    if(caps)
    {
        GstStructure *structure = gst_caps_get_structure(caps, 0);

        // Read the framerate value as a fraction
        gint numerator, denominator;
        gst_structure_get_fraction(structure, "framerate", &numerator, &denominator);

        // Calculate the framerate as an integer
        gint framerate = numerator / denominator;

        // Print the framerate value
        g_print("Framerate: %d\n", framerate);

        // Unreference the caps and clean up
        //gst_caps_unref(caps);

        if(framerate == 15)
        {
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, _WIDTH*2,
                                        "height", G_TYPE_INT, _HEIGHT,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
        }
        else if(framerate == 30)
        {
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, _WIDTH*2,
                                        "height", G_TYPE_INT, _HEIGHT,
                                        "framerate", GST_TYPE_FRACTION, 15, 1,
                                        NULL);
        }

        g_object_set(capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);
    }

    return TRUE;
}


static gboolean change_file_name(gpointer data) 
{
#ifdef FILESINK_ENABLE
    GstElement *sink = (GstElement *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    GstStateChangeReturn ret = gst_element_set_state(sink, GST_STATE_NULL);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_ERR, "[GST][%s:%d] filesink state change filed", _FILE_, __LINE__);
        return -1;
    }

    g_object_set(sink, "location", g_strdup_printf("output_%s-ch.mp4", date_str), NULL);

    gst_element_set_state(sink, GST_STATE_PLAYING);

#else

#if 0
    GstElement *sink = (GstElement *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    GstStateChangeReturn ret = gst_element_set_state(sink, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_ERR, "[GST][%s:%d] filesink state change filed", _FILE_, __LINE__);
        return -1;
    }

    g_object_set(sink, "location", g_strdup_printf("output_%s-ch.mp4", date_str), NULL);

    gst_element_set_state(sink, GST_STATE_PLAYING);
#endif

    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str;
#ifdef FILENAME_SEC_ZERO
    static gint min = -1;
    gint tmp = g_date_time_get_minute(datetime);

    if(tmp == min)
        return TRUE;

    min = tmp;
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

    if(info->file_name == NULL) 
    {
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
        info->file_name = g_strdup_printf("output_%s-ch%d.mp4", date_str, info->ch);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
        __LOG(LOG_NOTICE, "[GST][%s:%d] date : %s file_name : %s", _FILE_, __LINE__, date_str, info->file_name);
    }
    else
    {
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        info->file_name = g_strdup_printf("output_%s-ch%d.mp4", date_str, info->ch);
    }
#else
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    info->file_name = g_strdup_printf("output_%s-ch%d.mp4", date_str, info->ch);
    __LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
#endif  //FILENAME_SEC_ZERO
    
    g_date_time_unref(datetime);
    g_free(date_str);

#endif  //FILESINK_ENABLE

    return TRUE;
}
// appsink의 "new-sample" 시그널 처리 함수
static GstFlowReturn new_sample_handler_file(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)data;
    //GstClockTime timestamp;

    // sample을 가져오기
    //g_signal_emit_by_name(appsink, "pull-sample", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        __LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }

    // 버퍼 가져오기
    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    //timestamp = gst_clock_get_time(gst_system_clock_obtain());
    //gst_buffer_set_timestamp(buffer, timestamp);
    //GST_BUFFER_PTS(buffer) = timestamp;
    //GST_BUFFER_DTS(buffer) = timestamp;

#if 0
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
    // 녹화 시작 시간을 설정하고, 처음 버퍼를 받았을 때의 타임스탬프를 기준으로 프레임의 타임스탬프를 조정
    static GstClockTime recording_start_time = GST_CLOCK_TIME_NONE;
    if (recording_start_time == GST_CLOCK_TIME_NONE) {
        recording_start_time = timestamp;
    }

    // 녹화 시작 시간을 기준으로 타임스탬프 조정
    timestamp = timestamp - recording_start_time;
    GST_BUFFER_TIMESTAMP(buffer) = timestamp;

    //__LOG(LOG_DEBUG, "[GST][%s:%d] timestamp : %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT "\n", _FILE_, __LINE__, \
                                GST_TIME_ARGS(timestamp), GST_TIME_ARGS(GST_BUFFER_DURATION(buffer)));
    g_print("timestamp : %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(timestamp));
    static GstClockTime prev_timestamp = GST_CLOCK_TIME_NONE;
    static guint frame_counter = 0;
    g_print("frame_counter:%d\n",frame_counter);
    GstClockTime frame_duration = gst_util_uint64_scale_int(1, GST_SECOND, FPS); // 30 fps로 가정
    //g_print("duration : %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(frame_duration));
    //g_print("prev_timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(prev_timestamp));
    if (prev_timestamp != GST_CLOCK_TIME_NONE) {
        GstClockTime time_difference = timestamp - prev_timestamp;
        g_print("time_difference: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(time_difference));
        guint frames_passed = time_difference / frame_duration;
        frame_counter += frames_passed;
    }
    prev_timestamp = timestamp;
    GST_BUFFER_DTS(buffer) = timestamp;
#endif

#if 0
    static guint64 total_duration = 0;
    GstClockTime duration = GST_BUFFER_DURATION(buffer);
    total_duration += duration;
    if (total_duration >= FILE_SAVE_DURATION * GST_SECOND)
    {
        //change_file_name(info);
        info->file_name = g_strdup_printf("%s", info->file_name_tmp);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] total_duration : %" GST_TIME_FORMAT "\n", _FILE_, __LINE__, GST_TIME_ARGS(total_duration));
        total_duration = 0;
        //GST_BUFFER_TIMESTAMP(buffer) = GST_CLOCK_TIME_NONE;
    }
#endif
    // 파일로 저장
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    //g_print("file_name:%s\n", info->file_name);
    FILE *file = fopen(info->file_name, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, info->file_name);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

gboolean gst_rtsp_stream_server_push_data(gpointer userData, gpointer data, gint size, gboolean key_frame_flag, GstBuffer* buffer)
{
	CustomData *info = (CustomData *)userData;
	GstBuffer			*buf = NULL;
	GstMapInfo 			map;
	GstFlowReturn 		flow_ret;

	gboolean 	bret;

	if(!info || !data || (size <= 0)) 
	{
		g_printerr("invalid param - handle(%p) data(%p) size(%d)\n", info, data, size);
		goto error;
	}

	//g_mutex_lock(&stream->lock);
#if 0
	if(!info->appsrc)
		goto done;
#endif
	if((GST_STATE_NULL == GST_STATE(GST_ELEMENT(info->appsrc)))
			|| (GST_STATE_READY == GST_STATE(GST_ELEMENT(info->appsrc)))) 
	{
		goto done;
	}


#if 0
	if(info->set_caps) 
	{
		g_object_set(stream->appsrc, "caps", stream->set_caps, NULL);
		stream->set_caps = NULL;
		DBG_LIB_RTSP_STREAM_SERVER(1, "set caps\n");
	}
#endif

#if 0
	if(!info->push_1st_iframe) 
	{
		if(!key_frame_flag) 
		{
			goto done;
		}
	}
#endif

	buf = gst_buffer_new_and_alloc(size);

	bret = gst_buffer_map(buf, &map, GST_MAP_WRITE);
	if(!bret) 
	{
		g_printerr("map fail!\n");
		goto error;
	}

	memcpy(map.data, data, size);
	map.size = size;

	gst_buffer_unmap(buf, &map);
#if 0
	flow_ret = gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), buf);
	if(flow_ret != GST_FLOW_OK) 
	{
		g_printerr("push_buffer fail!\n");
		goto error;
	}
#endif
    //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));

    gst_buffer_copy_into(buf, buffer, GST_BUFFER_COPY_MEMORY, 0, -1);     
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), buf);
    //gst_buffer_unref (buf);
#if 0
	if(!info->push_1st_iframe) 
	{
		if(key_frame_flag) 
		{
			info->push_1st_iframe = TRUE;
			//DBG_LIB_RTSP_STREAM_SERVER(1, "push 1st iframe ###\n");
		}
	}
#endif


done:
	//g_mutex_unlock(&stream->lock);
	return TRUE;

error:
	//g_mutex_unlock(&stream->lock);

	return FALSE;
}

GstFlowReturn rtsp_stream_new_sample(GstElement *sink, gpointer userData)
{
	CustomData *info = (CustomData *)userData;
	//struct rtsp_stream		*rtsp;

	GstSample				*sample;
	GstPad					*pad;
	GstMapInfo				map;
	GstCaps					*caps;

	gint					handle_index = (-1);
	gchar					stream_name[32];
	gboolean				key_frame_flag, bret;

	//DBG_RTSP_STREAM(2, "sink name : %s\n", gst_element_get_name(sink));

	if( (info == NULL) || (sink == NULL) )
		return GST_FLOW_ERROR;

	//rtsp = &info->rtsp;
	handle_index = info->ch;

	//DBG_RTSP_STREAM(2, "Camera Rtsp Stream Channel : %d\n", handle_index);
#if 0
	if(!info->get_1st_frame) 
	{
		pad = gst_element_get_static_pad(sink, "sink");
		caps = gst_pad_get_current_caps(pad);

		info->get_1st_frame = TRUE;

		//sprintf(stream_name, "ch%d", handle_index);
		//bret = gst_rtsp_stream_server_start(rtsp->handle, stream_name, rtsp->user);

		//gst_rtsp_stream_server_set_caps(rtsp->handle, caps);

		gst_caps_unref(caps);
		gst_object_unref(pad);
	}
#endif

	/* Retrieve the buffer */
	sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
	if(sample) 
	{
		GstBuffer *buffer = gst_sample_get_buffer (sample);
#if 0
		if(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) 
			key_frame_flag = FALSE;
		else 
			key_frame_flag = TRUE; 
#endif
		bret = gst_buffer_map(buffer, &map, GST_MAP_READ);
		if(!bret) 
		{
			g_printerr("gst_buffer_map fail !!!\n");
			return GST_FLOW_ERROR;
		}

		gpointer data = map.data;
		gint size = map.size;

        //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));        
		//gst_buffer_unmap(buffer, &map);

		gst_rtsp_stream_server_push_data(info, data, size, key_frame_flag, buffer);
        gst_buffer_unmap(buffer, &map);
		gst_sample_unref (sample);
		return GST_FLOW_OK;
	}

	return GST_FLOW_ERROR;
}

static GstFlowReturn new_sample_handler_rtsp(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)data;
    static gboolean start_f = FALSE;
    //static gboolean push_1st_iframe = FALSE;
    gboolean key_frame_flag, bret;
    static GstClockTime recording_start_time = GST_CLOCK_TIME_NONE;
    GstMapInfo map;

    if(!start_f)
    {
        GstPad *pad = gst_element_get_static_pad(sink, "sink");

        if(!pad){
            g_print("caps NULL\n");
            gst_object_unref(pad);
            return GST_FLOW_ERROR;
        }    

        GstCaps *caps = gst_pad_get_current_caps(pad);

        if(!caps){
            g_print("caps NULL\n");
            gst_caps_unref(caps);
            gst_object_unref(pad);
            return GST_FLOW_ERROR;
        }

        gchar *caps_str = gst_caps_to_string(caps);
        //g_print("Current caps: %s\n", caps_str);
        //info->caps = caps; 

        g_object_set(info->appsrc, "caps", caps, NULL);
        g_free(caps_str);
        gst_caps_unref(caps);
        gst_object_unref(pad);
        start_f = TRUE;
    }

    // sample을 가져오기
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    //g_signal_emit_by_name(sink, "pull-preroll", &sample);
    //sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        __LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }


	if(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) 
		key_frame_flag = FALSE;
	else 
		key_frame_flag = TRUE;


	bret = gst_buffer_map(buffer, &map, GST_MAP_READ);
	if(!bret) 
	{
		g_printerr("gst_buffer_map fail !!!\n");
		return GST_FLOW_ERROR;
	}    

    if(!info->appsrc)
    {
        //__LOG(LOG_ERR, "[GST][%s:%d] src NULL", _FILE_, __LINE__);
        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_OK;
    }

    GstBuffer *buf = NULL;
    buf = gst_buffer_new_and_alloc(map.size);
	bret = gst_buffer_map(buf, &map, GST_MAP_WRITE);
	if(!bret) 
	{
        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &map);
		g_printerr("map fail!\n");
		return GST_FLOW_ERROR;
	}
    gst_buffer_unmap(buf, &map);

    //__LOG(LOG_NOTICE, "[GST][%s:%d] src exist", _FILE_, __LINE__);

#if 1
	if(GST_STATE(GST_ELEMENT(info->appsrc)) < GST_STATE_PAUSED)
	{

		return GST_FLOW_OK;
	}
#endif

#if 0
    // 버퍼 가져오기
    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
#endif

    //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));
    if(gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), buf) != GST_FLOW_OK)
    {
        g_printerr("push fail!\n");
    }

    gst_sample_unref(sample);


    return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) {
    // Preroll frame 처리 작업 추가 가능
    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    //g_print("Preroll frame\n");
    __LOG(LOG_NOTICE, "[GST][%s:%d] new_preroll_handler", _FILE_, __LINE__, info->file_name);
    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "sink0") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->ch = 0;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
        __LOG(LOG_NOTICE, "[GST][%s:%d] sink_name:%s channel:%d", _FILE_, __LINE__, sink_name, info->ch);
    }
    else if(g_str_equal(sink_name, "sink1") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->ch = 1;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
        __LOG(LOG_NOTICE, "[GST][%s:%d] sink_name:%s channel:%d", _FILE_, __LINE__, sink_name, info->ch);
    }
    else
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink name %s is not matching", _FILE_, __LINE__, sink_name);
        return GST_FLOW_ERROR;
    }
    g_free (sink_name);
    change_file_name(info);
#ifdef FILENAME_SEC_ZERO
    g_timeout_add(100, (GSourceFunc)change_file_name, info);
#else
    g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)change_file_name, info);
#endif
    return GST_FLOW_OK;
}


void sigintHandler(int unused) {
	__LOG(LOG_NOTICE, "[GST][%s:%d] sigintHandler", _FILE_, __LINE__);
	gst_element_send_event(pipeline, gst_event_new_eos());
    destroy();
	return;
}

void sigkillHandler(int unused) {
	__LOG(LOG_NOTICE, "[GST][%s:%d] sigkillHandler", _FILE_, __LINE__);
	gst_element_send_event(pipeline, gst_event_new_eos());
    destroy();
	return;
}

void sigtermHandler(int unused) {
	__LOG(LOG_NOTICE, "[GST][%s:%d] sigtermHandler", _FILE_, __LINE__);
	gst_element_send_event(pipeline, gst_event_new_eos());
    destroy();
	return;
}

void sigHandler(int sig) {
	__LOG(LOG_NOTICE, "[GST][%s:%d] sigHandler", _FILE_, __LINE__);

    if(sig == SIGINT) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGINT", _FILE_, __LINE__);
    else if(sig == SIGKILL) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGKILL", _FILE_, __LINE__);
    else if(sig == SIGTERM) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGTERM", _FILE_, __LINE__);

    destroy();
    gst_element_send_event(pipeline, gst_event_new_eos());
	return;
}

void attachInterruptHandlers()
{
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTERM, sigHandler);
}

/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  GST_INFO ("source %p in session %p is active", source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("structure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

static void on_sender_ssrc_active (GObject * session, GObject * source,
    GstRTSPMedia * media)
{
  GstStructure *stats;

  GST_INFO ("source %p in session %p is active", source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("Sender stats:\nstructure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb (GstRTSPMedia * media)
{
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams (media);

  GST_INFO ("media %p is prepared and has %u streams", media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession (stream);
    GST_INFO ("watching session %p on stream %u", session, i);

    g_signal_connect (session, "on-ssrc-active",
        (GCallback) on_ssrc_active, media);
    g_signal_connect (session, "on-sender-ssrc-active",
        (GCallback) on_sender_ssrc_active, media);
  }
}

static void	media_constructed(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
    CustomData *info = (CustomData*)user_data;
	GstElement *element;
    //GstElement *queue;
    gchar *queue_name;
    //info->queue
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    queue_name = g_strdup_printf("%s%d", QUEUE_NAME, info->ch);
    GstElement *queue = gst_bin_get_by_name(GST_BIN(pipeline), queue_name);

    g_print("queue_name : %s\n", queue_name);

    if (queue) {
        gst_element_send_event(queue, gst_event_new_flush_start());
        gst_element_send_event(queue, gst_event_new_flush_stop(TRUE));
        g_print("Queue flushed\n");
    }

    queue_name = g_strdup_printf("%s%d", QUEUE_RTSP_NAME, info->ch);
    queue = gst_bin_get_by_name(GST_BIN(pipeline), queue_name);
    

    //gst_object_unref(queue);
    g_free(queue_name);

    return;
}

static void	media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
	//GstElement *src = (GstElement*)user_data;
    CustomData *info = (CustomData*)user_data;
	GstElement *element;
    //GstElement *queue;
    gchar *appsrc_name;
    //gchar *queue_name;
    //static GMutex mutex;

	//g_mutex_lock(&mutex);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
	element = gst_rtsp_media_get_element(media);
    //name = GST_ELEMENT_NAME(GST_APP_SRC(element));
    //name = gst_object_get_name(GST_OBJECT(element));
    appsrc_name = g_strdup_printf("%s%d", APPSRC_NAME, info->ch);
    __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	info->appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), appsrc_name);

    //queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	//queue = gst_bin_get_by_name_recurse_up(GST_BIN(element), queue_name);

    //g_object_set (G_OBJECT (queue), 

    g_free(appsrc_name);
    //g_free(queue_name);
	gst_object_unref(element);

    //g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
	//g_mutex_unlock(&mutex);
    return;
}

static gboolean timeout(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;

   __LOG(LOG_DEBUG, "[GST][%s:%d] rtsp session pool", _FILE_, __LINE__);

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

gint setRtspPipe(gpointer user_data)
{
  //GgstLoop *loop;
  //GstRTSPServer *server;
  //GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  CustomData *info = (CustomData*)user_data;

  //gst_init (&argc, &argv);
  //g_print("info.ch:%d info.filename:%d\n", info->ch, info->file_name);
  __LOG(LOG_NOTICE, "[GST][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);

  //GgstLoop *loop = g_main_loop_new (NULL, FALSE);
  //info->rtspLoop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  //server = gst_rtsp_server_new ();

  //g_object_set (server, "service", "8554", NULL);
  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  //mounts = gst_rtsp_server_get_mount_points (rtspServer);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  //factory1 = gst_rtsp_media_factory_new ();
  
  __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s%d", _FILE_, __LINE__, APPSRC_NAME, info->ch);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s is-live=1 ! queue ! h264parse \
                    ! rtph264pay name=pay0 config-interval=-1 )", info->appsrc_name);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s%d is-live=1 ! queue max-size-time=0 max-size-buffers=1 ! h264parse ! rtph264pay name=pay0 )", APPSRC_NAME, info->ch);
  gchar *launch_str = g_strdup_printf("( appsrc name=%s%d is-live=1 do-timestamp=1 ! queue name=%s%d max-size-buffers=10 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", APPSRC_NAME, info->ch, QUEUE_RTSP_NAME, info->ch);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s%d max-bytes=0 do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=10 leaky=2 \
			! h264parse ! rtph264pay name=pay0 config-interval=-1 )", APPSRC_NAME, info->ch);
  gst_rtsp_media_factory_set_launch(factory, launch_str);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_free(launch_str);

  //gst_rtsp_media_factory_set_launch(factory1, launch_str);
  //gst_rtsp_media_factory_set_shared(factory1, TRUE);
  /* notify when our media is ready, This is called whenever someone asks for
   * the media and a new pipeline with our appsrc is created */
  g_signal_connect (factory, "media-configure", (GCallback) media_configure, info);
  //g_signal_connect (factory, "media-constructed", (GCallback) media_constructed, info);

  __LOG(LOG_NOTICE, "[GST][%s:%d] suspend_mode : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_suspend_mode(factory));
  //gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_PAUSE);

  /* attach the test factory to the /test url */
  gchar *point = g_strdup_printf("/ch%d", info->ch);
  __LOG(LOG_NOTICE, "[GST][%s:%d] point : %s", _FILE_, __LINE__, point);

  gst_rtsp_mount_points_add_factory (rtspMounts, point, factory);
  g_free(point);

  //gst_rtsp_mount_points_add_factory (mounts, "/ch1", factory1);

  /* don't need the ref to the mounts anymore */
  //g_object_unref (mounts);

  /* attach the server to the default maincontext */
  //if (gst_rtsp_server_attach (server, NULL) == 0) goto failed;

  //g_timeout_add_seconds (2, (GSourceFunc) timeout, server);
  /* start serving */
  //g_print ("stream ready at rtsp://127.0.0.1:8554/ch0\n");
  __LOG(LOG_NOTICE, "[GST][%s:%d]stream ready at rtsp://127.0.0.1:8554%s", _FILE_, __LINE__, point);

  return GST_FLOW_OK;

failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}

static gboolean handle_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) {
    // This function is called when a new client is connected to the RTSP server.
    // You can handle the client connection here.
    const gchar *ip_address = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, ip_address);
    
    // Initialize session for the new client
    
    return TRUE;
}

static gboolean handle_client_disconnected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) {
    // This function is called when a client is disconnected from the RTSP server.
    // You can clean up resources associated with the client session here.
    const gchar *ip_address = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, ip_address);
    // Clean up session for the disconnected client
    
    return TRUE;
}

static gboolean eos_callback(GstAppSink *appsink, gpointer user_data) {
    g_print("%s\n", __FUNCTION__);
    return FALSE;
}

static gboolean underrun_callback(GstAppSink *appsink, gpointer user_data) {
    g_print("%s\n", __FUNCTION__);
    return FALSE;
}

static gboolean overrun_callback(GstAppSink *appsink, gpointer user_data) {
    g_print("%s\n", __FUNCTION__);
    return FALSE;
}

static gboolean running_callback(GstAppSink *appsink, gpointer user_data) {
    g_print("%s\n", __FUNCTION__);
    return FALSE;
}

static gboolean pushing_callback(GstAppSink *appsink, gpointer user_data) {
    g_print("%s\n", __FUNCTION__);
    return FALSE;
}

int main(int argc, char *argv[]) {

    gst_init(&argc, &argv);
    attachInterruptHandlers();

    CustomData customData[2];

    customData[0].file_name = NULL;
    customData[0].appsrc = gst_element_factory_make("appsrc", "APPSRC2_NAME");
    customData[0].appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    customData[0].ch = 0;
    customData[1].file_name = NULL;
    customData[1].appsrc = gst_element_factory_make("appsrc", "APPSRC3_NAME");
    customData[1].appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    customData[1].ch = 1;
    //g_allocator_new

    rtspServer = gst_rtsp_server_new ();
    g_object_set (rtspServer, "service", "8554", NULL);

    rtspMounts = gst_rtsp_server_get_mount_points (rtspServer);

    g_signal_connect(rtspServer, "client-connected", G_CALLBACK(handle_client_connected), NULL);
    g_signal_connect(rtspServer, "client-disconnected", G_CALLBACK(handle_client_disconnected), NULL);

    g_timeout_add_seconds (5, (GSourceFunc) timeout, rtspServer);

    if (gst_rtsp_server_attach (rtspServer, NULL) == 0) __LOG(LOG_CRIT, "[GST][%s:%d] rtsp server attach failed", _FILE_, __LINE__);

    // 파이프라인 생성
    pipeline = gst_pipeline_new("pipeline");

    // 요소 생성
    GstElement *src = gst_element_factory_make("v4l2src", "src");
    //GstElement *src = gst_element_factory_make("videotestsrc", "src");
    GstElement *convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    GstElement *convert0 = gst_element_factory_make("imxvideoconvert_g2d", "convert0");
    GstElement *videorate0 = gst_element_factory_make("videorate", "videorate0");
    GstElement *videorate1 = gst_element_factory_make("videorate", "videorate1");
    GstElement *videorate2 = gst_element_factory_make("videorate", "videorate2");
    GstElement *videorate3 = gst_element_factory_make("videorate", "videorate3");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "caps");
    GstElement *capsfilter0 = gst_element_factory_make("capsfilter", "caps0");
    GstElement *encoder0 = gst_element_factory_make("vpuenc_h264", "encoder0");
    GstElement *encoder1 = gst_element_factory_make("vpuenc_h264", "encoder1");
    GstElement *encoder2 = gst_element_factory_make("vpuenc_h264", "encoder2");
    GstElement *encoder3 = gst_element_factory_make("vpuenc_h264", "encoder3");
    GstElement *queue0 = gst_element_factory_make("queue", "queue0");
    GstElement *queue1 = gst_element_factory_make("queue", "queue1");
    GstElement *queue2 = gst_element_factory_make("queue", "queue2");
    GstElement *queue3 = gst_element_factory_make("queue", "queue3");
#if defined(SPLITSINK_ENABLE)
    GstElement *appsink0 = gst_element_factory_make("splitmuxsink", "sink0");
#elif defined(FILESINK_ENABLE)
    GstElement *appsink0 = gst_element_factory_make("filesink", "sink0");
#else
    GstElement *appsink0 = gst_element_factory_make("appsink", "sink0");
#endif
    GstElement *appsink1 = gst_element_factory_make("appsink", "sink1");
    GstElement *appsink2 = gst_element_factory_make("appsink", "sink2");
    GstElement *appsink3 = gst_element_factory_make("appsink", "sink3");
    GstElement *tee = gst_element_factory_make("tee", "tee");
    GstElement *crop0 = gst_element_factory_make("videocrop", "crop0");
    GstElement *crop1 = gst_element_factory_make("videocrop", "crop1");
    GstElement *crop2 = gst_element_factory_make("videocrop", "crop2");
    GstElement *crop3 = gst_element_factory_make("videocrop", "crop3");
    GstElement *tee1 = gst_element_factory_make("tee", "tee1");
    GstElement *tee2 = gst_element_factory_make("tee", "tee2");
    GstElement *parse0 = gst_element_factory_make("h264parse", "parse0");
    GstElement *parse1 = gst_element_factory_make("h264parse", "parse1");
    GstElement *parse2 = gst_element_factory_make("h264parse", "parse2");
    GstElement *parse3 = gst_element_factory_make("h264parse", "parse3");
    GstElement *mux = gst_element_factory_make("mp4mux", "mux");

    // 요소가 생성되지 않은 경우 에러 처리
    if (!pipeline || !convert || !src || !capsfilter || !capsfilter0 || !encoder0 || !encoder1 || !encoder2 || !encoder3 || !queue0 || !queue1 || !queue2 || !queue3 
    || !appsink0 || !appsink1 || !appsink2 || !appsink3 || !tee || !crop0 || !crop1 || !crop2 || !crop3 || !tee1 || !tee2 || !parse0 || !parse1 
    || !mux || !videorate0 || !videorate1 || !videorate2 || !videorate3 || !convert0) 
    {
        g_printerr("1.\n");
        return -1;
    }
    gst_bin_add_many(GST_BIN(pipeline), parse0, mux, convert0, capsfilter0, NULL);

    // 파이프라인에 요소 추가
    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, convert, tee, parse0, mux, NULL);

    // 요소 연결
    if (!gst_element_link_many(src, capsfilter, tee, NULL)) {
        g_printerr("main link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    g_object_set(src, "do-timestamp", TRUE, NULL);

#ifdef FILE0_ENABLE
    gst_bin_add_many(GST_BIN(pipeline), crop0, queue0, videorate0, encoder0, appsink0, NULL);
    if (!gst_element_link_many(tee, crop0, queue0, videorate0, encoder0, appsink0, NULL)) {
        g_printerr("0 link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    g_object_set(crop0, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    //g_object_set(videorate0, "max-rate", FILE_FPS, NULL);
    //g_object_set(videorate0, "drop-only", FALSE, NULL);
    g_object_set(encoder0, "bitrate", 4096, NULL);
    g_object_set(appsink0, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(appsink0, "new-sample", G_CALLBACK(new_sample_handler_file), &customData[0]);
    g_signal_connect(appsink0, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[0]);

#endif
    
#ifdef RTSP_TEST
    GstRTSPServer *server = gst_rtsp_server_new();
    gst_rtsp_server_attach(server, NULL);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    gchar *launch_str = g_strdup_printf("( appsrc name=%s is-live=1 ! queue ! h264parse ! rtph264pay name=pay0 pt=96 )", info->appsrc_name);
    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    //gst_rtsp_server_mount_factory(server, "/ch0", factory);
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points (server);
    gst_rtsp_mount_points_add_factory (mounts, "/ch0", factory);
    gst_bin_add(GST_BIN(pipeline), GST_BIN(server));
    GstElement *rtsp_pipeline = gst_pipeline_new("rtsp-server-pipeline");
    gst_bin_add_many(GST_BIN(rtsp_pipeline), server, rtsp_sink, NULL);
    if (!gst_element_link_many(tee, crop2, queue2, encoder2, GST_BIN(server), NULL)) {
            g_printerr("server link err.\n");
            gst_object_unref(pipeline);
            return -1;
    }
    gst_rtsp_media_new

    // Link the media pipeline output to the RTSP server pipeline input
    GstPad *srcpad = gst_element_get_static_pad(encoder2, "src");
    GstPad *sinkpad = gst_element_get_static_pad(rtsp_server, "sink");
    gst_pad_link(srcpad, sinkpad);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

#endif

#ifdef RTSP0_ENABLE
    gst_bin_add_many(GST_BIN(pipeline), crop2, queue2, videorate2, encoder2, parse2, appsink2, parse3, queue3, NULL);
    if (!gst_element_link_many(tee, queue2, crop2, videorate2, encoder2, appsink2, NULL)) 
    {
        g_printerr("2 link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    g_object_set(crop2, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    g_object_set(videorate2, "max-rate", RTSP_FPS, NULL);
    g_object_set(videorate2, "drop-only", FALSE, NULL);
    g_object_set(encoder2, "bitrate", 1024, NULL);
    //g_object_set(encoder2, "gop-size", 30, NULL);
    g_object_set(appsink2, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(appsink2, "max-lateness", 5000000000, NULL);


#if 1
	//g_object_set(queue2, "max-size-bytes", 0, NULL);
	//g_object_set(queue2, "max-size-time", 1000000000, NULL);
	g_object_set(queue2, "max-size-buffers", 10, NULL);
	g_object_set(queue2, "leaky", 2, NULL);
	//g_object_set(appsink2, "max-buffers", 1, NULL);
    //g_object_set(queue2, "flush-on-eos", TRUE, NULL);
    //g_signal_connect(queue2, "underrun", G_CALLBACK(underrun_callback), NULL);
    //g_signal_connect(queue2, "overrun", G_CALLBACK(overrun_callback), NULL);
    //g_signal_connect(queue2, "running", G_CALLBACK(running_callback), NULL);
    //g_signal_connect(queue2, "pushing", G_CALLBACK(pushing_callback), NULL);

#endif

    
    g_object_set(appsink2, "max-buffers", 10, NULL);
    g_object_set(appsink2, "drop", FALSE, NULL);
    //g_object_set(appsink2, "enable-last-sample", TRUE, NULL);
    //g_object_set(appsink2, "max-lateness", 1000, NULL);
    //g_object_set(appsink2, "throttle-time", 100, NULL);
    //g_object_set(appsink2, "processing-deadline", 200, NULL);
    //g_object_set(appsink2, "render-delay", 1000000000, NULL);
    //g_object_set(appsink2, "wait-on-eos", FALSE, NULL);
    //g_object_set(appsink2, "qos", TRUE, NULL);
    g_object_set(appsink2, "sync", FALSE, NULL);
    //g_object_set(appsink2, "max-bitrate", 2048, NULL);
    g_signal_connect(appsink2, "eos", G_CALLBACK(eos_callback), NULL);

    g_signal_connect(appsink2, "new-sample", G_CALLBACK(rtsp_stream_new_sample), &customData[0]);
    //g_signal_connect(appsink2, "new-preroll", G_CALLBACK(new_preroll_handler), &info[0]);
    setRtspPipe(&customData[0]);
#endif

#ifdef RTSP1_ENABLE
    gst_bin_add_many(GST_BIN(pipeline), crop3, queue3, videorate3, encoder3, parse3, appsink3, parse3, queue3, NULL);
    if (!gst_element_link_many(tee, queue3, crop3, videorate3, encoder3, appsink3, NULL)) 
    {
        g_printerr("3 link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    g_object_set(crop3, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    g_object_set(videorate3, "max-rate", RTSP_FPS, NULL);
    g_object_set(videorate3, "drop-only", FALSE, NULL);
    g_object_set(encoder3, "bitrate", 1024, NULL);
    //g_object_set(encoder2, "gop-size", 30, NULL);
    //g_object_set(appsink3, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(appsink2, "max-lateness", 5000000000, NULL);


#if 1
	//g_object_set(queue2, "max-size-bytes", 0, NULL);
	//g_object_set(queue2, "max-size-time", 1000000000, NULL);
	g_object_set(queue3, "max-size-buffers", 10, NULL);
	g_object_set(queue3, "leaky", 2, NULL);
	//g_object_set(appsink2, "max-buffers", 1, NULL);
    //g_object_set(queue2, "flush-on-eos", TRUE, NULL);
    //g_signal_connect(queue2, "underrun", G_CALLBACK(underrun_callback), NULL);
    //g_signal_connect(queue2, "overrun", G_CALLBACK(overrun_callback), NULL);
    //g_signal_connect(queue2, "running", G_CALLBACK(running_callback), NULL);
    //g_signal_connect(queue2, "pushing", G_CALLBACK(pushing_callback), NULL);

#endif

    
    g_object_set(appsink3, "max-buffers", 10, NULL);
    g_object_set(appsink3, "drop", FALSE, NULL);
    //g_object_set(appsink2, "enable-last-sample", TRUE, NULL);
    //g_object_set(appsink2, "max-lateness", 1000, NULL);
    //g_object_set(appsink2, "throttle-time", 100, NULL);
    //g_object_set(appsink2, "processing-deadline", 200, NULL);
    //g_object_set(appsink2, "render-delay", 1000000000, NULL);
    //g_object_set(appsink2, "wait-on-eos", FALSE, NULL);
    //g_object_set(appsink2, "qos", TRUE, NULL);
    g_object_set(appsink3, "emit-signals", TRUE, "sync", FALSE, NULL);
    //g_object_set(appsink2, "max-bitrate", 2048, NULL);
    g_signal_connect(appsink3, "eos", G_CALLBACK(eos_callback), NULL);

    g_signal_connect(appsink3, "new-sample", G_CALLBACK(rtsp_stream_new_sample), &customData[1]);
    //g_signal_connect(appsink2, "new-preroll", G_CALLBACK(new_preroll_handler), &info[0]);
    setRtspPipe(&customData[1]);
#endif

    // 캡처 포맷 설정 (video3 장치의 적절한 캡처 포맷에 맞게 변경)
    g_object_set(src, "device", "/dev/video3", NULL);
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, _WIDTH*2,
                                        "height", G_TYPE_INT, _HEIGHT,
                                        "framerate", GST_TYPE_FRACTION, MAIN_FPS, 1,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    //GstStateChangeReturn ret = gst_element_set_state(pipe, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        return -1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
	if(!bus) {
        __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
    }
    guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);


    gstLoop = g_main_loop_new(NULL, FALSE);
	if(!gstLoop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp loop create error", _FILE_, __LINE__);
    }
    __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop start", _FILE_, __LINE__);
    g_main_loop_run(gstLoop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline); 
    g_main_loop_unref(gstLoop);
    g_object_unref(rtspServer);

    return 0;
}