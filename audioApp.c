#include <gst/gst.h>
#include <syslog.h>
#include <stdio.h>
//#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
//#include <gst/check/gstcheck.h>

#define JHW_TEST

void mylog( int opt, const char* _szfmt, ... );
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define CHARNEXT(x,y)    (strrchr(x,y)? strrchr(x,y)+1:x)
#define _FILE_  CHARNEXT(__FILE__, '/')
#define FILE_SAVE_DURATION  60

#ifdef JHW_TEST
#define FILE_PATH   ""
#else
#define FILE_PATH   "/mnt/sd_cam/"
#endif

GMainLoop *loop;
GstElement *pipeline;

guint8 log_level = 7;
guint8 dbg_level = 7;
guint8 sigflag = 0;

gchar *program_name;
gchar *ohtName;

typedef struct _CustomData{
    gchar* file_name;
    gchar* date;
    gchar* time;
    //gchar* appsrc_name;
    guint8 index;
    guint8 ch;
    guint8 min;
    GstElement *appsrc;
    GstElement *appsink;
    GstElement *enc;
    GstElement *vr;
    GstElement *mux;
    //GstElement *queue;
    //GstCaps *caps;
    GstBuffer *buf;
    guint16 captureCnt;
    guint16 captureMax;
    gboolean is_live;
    GstElement *bins;
    GstPad *bin_video_pads;
    GstPad *bin_audio_pads;
    //GgstLoop *rtspLoop;
    //GThread *rtspThread;
    //pthread_t m_threadRtsp;
} CustomData;

typedef struct AudioPipe{
    GstElement *src;
    GstElement *capsfilter;
    GstElement *convert;
    GstElement *resample;
    GstElement *encoder;
    GstElement *parse;
    GstElement *queue;
    GstElement *sink;
} AudioPipe;


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
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, program_name, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}

void sigHandler(int sig) {
    guint8 i;
    
	//__LOG(LOG_NOTICE, "[GST][%s:%d] sigHandler", _FILE_, __LINE__);

    //if(sig == SIGINT) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGINT", _FILE_, __LINE__);
    //else if(sig == SIGKILL) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGKILL", _FILE_, __LINE__);
    //else if(sig == SIGTERM) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGTERM", _FILE_, __LINE__);
    //ipc_clear();
    //destroy();
    //for(i=0; i<MAX_CAM; i++) gst_element_send_event(pipeline[i], gst_event_new_eos());
    printf("sigHandler\n");
    sigflag = 1;
    gst_element_send_event(pipeline, gst_event_new_eos());

	return;
}


void attachInterruptHandlers()
{
    //signal(SIGUSR1, ipcHandler);
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTERM, sigHandler);
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
    //PipeMain *info = (PipeMain *)data;
    static GstState state = GST_STATE_PLAYING;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS)
        return TRUE;

    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    if(GST_MESSAGE_TYPE(message) != GST_MESSAGE_STATE_CHANGED)
        __LOG(LOG_DEBUG, "[GST][%s:%d] Got %s message", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message));

    switch(GST_MESSAGE_TYPE(message)) 
    {
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);

            if(state != old_state)
            {
                g_print ("Pipeline state changed from %s to %s\n", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
                state = old_state;
            }
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
            break;
        }

        case GST_MESSAGE_EOS:
        {
            //printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
            __LOG(LOG_NOTICE, "[GST][%s:%d] GST_MESSAGE_EOS", _FILE_, __LINE__);
            if(sigflag) g_main_loop_quit(loop);
            else
            {
                gst_element_set_state(pipeline, GST_STATE_READY);
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
            }

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
#if 1
            gboolean live;
            guint64 running_time, stream_time,timestamp,duration;
            // gst_message_parse_qos (msg,&live,&running_time,&stream_time,&timestamp,&duration);
            // g_warning("GOt a QOS event %llu %llu %llu %llu", running_time, stream_time, timestamp, duration);
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %lld %f %lu", jitter, prop, qual );

            GstFormat format;
            guint64 processed;
            guint64 dropped;
            gst_message_parse_qos_stats(message, &format, &processed, &dropped);

            g_print("QoS Message:\n");
            g_print("Format: %s\n", gst_format_get_name(format));
            g_print("Processed: %llu\n", processed);
            g_print("Dropped: %llu\n", dropped);
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

        case GST_MESSAGE_BUFFERING: 
        {
            gint percent = 0;

            /* If the stream is live, we do not care about buffering. */
            //if (info->is_live) break;

            gst_message_parse_buffering (message, &percent);
            g_print ("Buffering (%3d%%)\r", percent);
            /* Wait until buffering is complete before start/resume playing */
#if 0
            if (percent < 100)
                gst_element_set_state (info->pipeline, GST_STATE_PAUSED);
            else
                gst_element_set_state (info->pipeline, GST_STATE_PLAYING);
            break;
#endif
        }

        case GST_MESSAGE_CLOCK_LOST:
        {
            /* Get a new clock */
            g_print ("GST_MESSAGE_CLOCK_LOST\r");
            //gst_element_set_state (info->pipeline, GST_STATE_PAUSED);
            //gst_element_set_state (info->pipeline, GST_STATE_PLAYING);
            break;
        }
        default:
            break;

    }

    return TRUE;
}

static gboolean change_file_name(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str;
#ifdef FILENAME_SEC_ZERO
    gint tmp = g_date_time_get_minute(datetime);

    if(tmp == info->min)
        return TRUE;

    info->min = tmp;
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    
    if(info->file_name == NULL) 
    {
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
        info->file_name = g_strdup_printf("%s_%s", ohtName, date_str);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
        __LOG(LOG_NOTICE, "[GST][%s:%d] date : %s file_name : %s", _FILE_, __LINE__, date_str, info->file_name);
    }
    else
    {
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        info->file_name = g_strdup_printf("%s_%s", ohtName, date_str);
    }
#else
    //gst_element_send_event(info->mux, gst_event_new_eos());
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    info->file_name = g_strdup_printf("%s_%s", ohtName, date_str);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
#endif  //FILENAME_SEC_ZERO
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    g_date_time_unref(datetime);
    g_free(date_str);

    return TRUE;
}

static GstFlowReturn new_sample_handler(GstElement *sink, gpointer userData) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)userData;
    guint8 mode;
    GstMapInfo map;
    gchar *path = NULL;
    FILE *file;
    //GstClockTime timestamp;

    // sample을 가져오기
    //g_signal_emit_by_name(appsink, "pull-sample", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }
    buffer = gst_sample_get_buffer(sample);
    gst_sample_unref(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        //gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        g_printerr("Failed to map buffer\n");
        return GST_FLOW_ERROR;
    }

    path = g_strdup_printf("%s%s.mp3", FILE_PATH, info->file_name);

    file = fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
    }

    gst_buffer_unmap(buffer, &map);
    //gst_sample_unref(sample);
    if(path!=NULL) g_free(path);

    return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) {
    // Preroll frame 처리 작업 추가 가능
    CustomData *info = (CustomData *)data;
    gchar *sink_name;
    info->appsink = sink;
    //g_print("Preroll frame\n");
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    sink_name = gst_object_get_name(GST_OBJECT(sink));

    __LOG(LOG_NOTICE, "[GST][%s:%d] sink_name:%s", _FILE_, __LINE__, sink_name);
    g_free (sink_name);

    change_file_name(info);

    //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)change_file_name, info);

    return GST_FLOW_OK;
}

static gboolean eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    CustomData *info = (CustomData *)user_data;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    GstState state;
    gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    //g_message("to : %s", gst_element_state_get_name(state));
    __LOG(LOG_NOTICE, "state : %s", _FILE_, __LINE__, gst_element_state_get_name(state));
    //change_file_datetime(NULL);
    return TRUE;
}

static gboolean sendEOS(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    gst_pad_send_event(info->bin_audio_pads, gst_event_new_eos());

    return TRUE;
}

static gboolean on_samples_selected(GstElement *element, GstSegment *arg0, guint64 arg1, guint64 arg2, guint64 arg3, GstStructure* arg4, gpointer user_data) {

    g_message ("%s", __FUNCTION__);
    return GST_FLOW_OK;
}


int main(int argc, char *argv[]) {
    // Initialize GStreamer
    gst_init(&argc, &argv);

    if(argc >= 1) {
        program_name = CHARNEXT(argv[0], '/');
        //g_printf("%s\n", program_name);
        __LOG(LOG_NOTICE, "[GST][%s:%d] %s\n", __FILE__, __LINE__, program_name);
        g_message("[GST][%s:%d] %s\n", __FILE__, __LINE__, program_name);
    }

    AudioPipe audioPipe;
    CustomData customData;

    attachInterruptHandlers();

    ohtName = g_strdup_printf("%s", "output");
    pipeline = gst_pipeline_new("my-pipeline");

    audioPipe.src = gst_element_factory_make("audiotestsrc", "audio-source");
    audioPipe.capsfilter = gst_element_factory_make("capsfilter", "caps");
    audioPipe.convert = gst_element_factory_make("audioconvert", "audio-convert");
    audioPipe.resample = gst_element_factory_make("audioresample", "audio-resample");
    audioPipe.encoder = gst_element_factory_make("lamemp3enc", "audio-encode");
    audioPipe.parse = gst_element_factory_make("mpegaudioparse", "audio-parse");
    //audioPipe.queue = gst_element_factory_make("queue2", "audio-queue");
    audioPipe.sink = gst_element_factory_make("appsink", "audio-sink");

    if(!audioPipe.src || !audioPipe.capsfilter || !audioPipe.convert || !audioPipe.resample || !audioPipe.encoder || !audioPipe.parse || !audioPipe.sink)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe create error", _FILE_, __LINE__);
        return -1;
    }
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] audio pipe create", _FILE_, __LINE__);

    g_object_set(audioPipe.src, "do-timestamp", TRUE, NULL);
    g_object_set(audioPipe.src, "is-live", TRUE, NULL);
    g_object_set(audioPipe.src, "tick-interval", 200000000, NULL);
    g_object_set(audioPipe.src, "wave", 8, NULL);

    g_object_set(audioPipe.sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(audioPipe.sink, "new-sample", G_CALLBACK(new_sample_handler), &customData);
    g_signal_connect(audioPipe.sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData);
    g_signal_connect(audioPipe.sink, "eos", G_CALLBACK(eos_callback), &customData);

    gst_bin_add_many(GST_BIN(pipeline), audioPipe.src, audioPipe.capsfilter, audioPipe.convert, audioPipe.resample, audioPipe.encoder, \
                    audioPipe.parse, audioPipe.sink, NULL);
    if(!gst_element_link_many(audioPipe.src, audioPipe.convert, audioPipe.resample, audioPipe.encoder, audioPipe.sink, NULL))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe link error", _FILE_, __LINE__);
        return -1;
    }
    __LOG(LOG_NOTICE, "[GST][%s:%d] audio pipe link", _FILE_, __LINE__);
    
    customData.bin_audio_pads = gst_element_get_static_pad(audioPipe.sink, "sink");

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", NULL);

    g_object_set(audioPipe.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstBus *bus = gst_element_get_bus(pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);

    g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)sendEOS, &customData);

    loop = g_main_loop_new(NULL, FALSE);

    g_print("Running...\n");
    //g_timeout_add_seconds(segment_duration, (GSourceFunc)sendEOS, fileSink);
    //g_timeout_add_seconds(10, (GSourceFunc)change_output_filename, mux);

    g_main_loop_run(loop);
#if 0
    // Wait until error or EOS
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                                 GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (msg != NULL)
        gst_message_unref(msg);
    gst_object_unref(bus);

    GstState state;
    GstState pending;
    GstClockTime timeout = 10 * GST_SECOND; // 10초 대기
    gst_element_get_state(pipeline, &state, &pending, timeout);
#endif
    g_print("Returned, stopping playback\n");
    // Release the message and the bus


    // Stop the pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);

    // Free resources
    gst_object_unref(pipeline);

    return 0;
}



