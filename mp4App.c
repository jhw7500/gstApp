#include <gst/gst.h>
#include <stdio.h>
#include <syslog.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/check/gstcheck.h>

#define FILENAME_SEC_ZERO


#define FILE_L_ENABLE
#define AUDIO_L_ENABLE
#define FILE_R_ENABLE
#define AUDIO_R_ENABLE
#define RTSP_L_ENABLE
#define RTSP_R_ENABLE
#define CAPTURE_L_ENABLEx
#define CAPTURE_R_ENABLEx
#define AUDIO_TESTx

#define TERMINAL_CMD_ENABLEx
#define RTSP_AUTH_ENABLEx

#define JHW_TESTx

#define FILE_L_APPSINKx
#define FILE_R_APPSINKx
#define MP4_TEST_APPSINKx

#define HIGH_BITRATE    4096
#define LOW_BITRATE     1024

#define APPSRC_NAME    "appsrc"

#define QUEUE_NAME    "queue"
#define QUEUE_RTSP_NAME    "queueRtsp"

#ifdef JHW_TEST
#define FILE_PATH   ""
#else
#define FILE_PATH   "/mnt/sd_cam/"
#endif

void mylog( int opt, const char* _szfmt, ... );
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define _FILE_  strrchr(__FILE__,'/')? strrchr(__FILE__,'/')+1:__FILE__
#define PROGRAM_NAME	"timeApp"
#define RTSP_PORT       "8554"
#define _WIDTH   1920
#define _HEIGHT  1080
#define MAIN_FPS 30
#define FILE_FPS 30
#define RTSP_FPS 15
#define DEFAULT_FPS     2147483647
#define FILE_BITRATE    4096
#define RTSP_BITRATE    1024
#define MAX_ENC 6
#define MAX_CAM 4
#define MAX_PIPELINE 2
#define FILE_SAVE_DURATION 60

static gboolean change_file_datetime() ;

GstElement *pipeline[2];
GMainLoop  *gstLoop;
GstRTSPServer *rtspServer;
GstRTSPMountPoints *rtspMounts;
GThread *ipcThread;
GThread *terminalThread;
GstPad *video_sink_pad[4];
GstPad *audio_sink_pad[4];
//GstElement *splitmuxsink[4];
//GstBus *bus[2];

volatile sig_atomic_t is_interrupted = 0;
volatile sig_atomic_t sigflag = 0;
gchar *fileDateTime = NULL;
gchar* ohtName;
unsigned char log_level = 7;
unsigned char dbg_level = 7;

typedef enum
{
  FILE0_L =  0,
  FILE0_R =  1,
  RTSP0_L =  2,
  RTSP0_R =  3,
  CAPTURE0_L = 4,
  CAPTURE0_R = 5,
  FILE1_L =  6,
  FILE1_R =  7,
  RTSP1_L =  8,
  RTSP1_R =  9,
  CAPTURE1_L = 10,
  CAPTURE1_R = 11
} PipeNum;

typedef enum
{
  NONE =  0,
  RECORDING =  1,
  STREAMING =  2,
  CAPTURING =  3
} VideoMode;

typedef struct AudioPipe{
    GstElement *src;
    GstElement *convert;
    GstElement *resample;
    GstElement *encoder;
    GstElement *parse;
    GstElement *queue;
    GstElement *tee;
} AudioPipe;

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
    GstElement *timeoveraly;
    //GstCaps *caps;
    GstBuffer *buf;
    guint16 captureCnt;
    guint16 captureMax;
    VideoMode mode;
    gboolean is_live;
    GstElement *bins;
    GstPad *bin_video_pads;
    GstPad *bin_audio_pads;
    gchar *client_ip;
    gboolean firstSplitFlag;
    //GgstLoop *rtspLoop;
    //GThread *rtspThread;
    //pthread_t m_threadRtsp;
} CustomData;
//CustomData info[4];

typedef struct _VideoPipe
{
    GstElement *crop;
    GstElement *sink;
    GstElement *encoder;
    GstElement *videorate;
    GstElement *queue;
    GstElement *queue2;
    GstElement *parse;
    GstElement *mux;
    GstElement *convert;
    AudioPipe audioPipe;
    GstElement *bins;
    GstElement *timeoverlay;
    //CustomData *customData;
} VideoPipe;

typedef struct _MainPipe
{
    GstElement *pipeline;
    GstElement *src;
    GstElement *tee;
    GstElement *capsfilter;
    GstElement *convert;
    //GstCaps *caps;
    GstBus *bus;
    guint bus_watch_id;
    gboolean is_live;
    VideoPipe videoPipe[MAX_ENC];
    //AudioPipe audioPipe;
    guint8 index;
    guint8 ch;
} PipeMain;

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
    guint8 i;
    //sleep(3);
    __LOG(LOG_EMERG, "[GST][%s:%d] destroy!!", _FILE_, __LINE__);
    is_interrupted = 1;
    sigflag = 0;
    //g_object_unref(rtspServer);
#if 0
    for(i=0;i<MAX_PIPELINE;i++)
    {
        gst_element_set_state(pipeline[i], GST_STATE_NULL);
        gst_object_unref(pipeline[i]); 
    }

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
#endif
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
    PipeMain *info = (PipeMain *)data;
    static guint8 cam_cnt = 0;

    static GstState state = GST_STATE_PLAYING;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS)
        return TRUE;


    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    if(GST_MESSAGE_TYPE(message) != GST_MESSAGE_STATE_CHANGED)
        __LOG(LOG_DEBUG, "[GST][%s:%d] Got %s message", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message));

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_STREAM_STATUS)
        return TRUE;

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
            gst_element_send_event(pipeline[info->index], gst_event_new_eos());
            break;
        }

        case GST_MESSAGE_EOS:
        {
            //printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
            __LOG(LOG_NOTICE, "[GST][%s:%d] GST_MESSAGE_EOS (index:%d sigflag:%d)", _FILE_, __LINE__, info->index, sigflag);
            if(sigflag)
            {
                cam_cnt++;
                if(cam_cnt >= MAX_PIPELINE) destroy();
            }
            else
            {
                gst_element_set_state(pipeline[info->index], GST_STATE_READY);
                //gst_element_get_state(pipeline[info->index], NULL, NULL, GST_CLOCK_TIME_NONE);
                //gst_element_seek(pipeline[info->index], 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);
                //gst_element_set_state(pipeline[info->index], GST_STATE_NULL);
                gst_element_set_state(pipeline[info->index], GST_STATE_PLAYING);
                //change_file_datetime(NULL);
            }

            break;
        }

        case GST_MESSAGE_ELEMENT:
            g_print("%s\n", gst_structure_to_string(gst_message_get_structure(message)));
            break;

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
            if (info->is_live) break;

            gst_message_parse_buffering (message, &percent);
            g_print ("Buffering (%3d%%)\r", percent);
            /* Wait until buffering is complete before start/resume playing */
#if 1
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
            gst_element_set_state (info->pipeline, GST_STATE_PAUSED);
            gst_element_set_state (info->pipeline, GST_STATE_PLAYING);
            break;
        }

        case GST_MESSAGE_LATENCY:
            {
                // when pipeline latency is changed, this msg is posted on the bus. we then have
                // to explicitly tell the pipeline to recalculate its latency
                // FIXME: this never works!
#if 1
                if (!gst_bin_recalculate_latency (GST_BIN(info->pipeline)))
                    g_print("Could not reconfigure latency.\n");
                else
                    g_print("Reconfigured latency.\n");
                break;
#endif

            }        
        default:
            break;

    }

    return TRUE;
}

/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] source %p in session %p is active", _FILE_, __LINE__, source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("structure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

static void on_sender_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] source %p in session %p is active", _FILE_, __LINE__, source, session);

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

  //GST_INFO ("media %p is prepared and has %u streams", media, n_streams);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] media %p is prepared and has %u streams", _FILE_, __LINE__, media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession (stream);
    //GST_INFO ("watching session %p on stream %u", session, i);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] watching session %p on stream %u", _FILE_, __LINE__, session, i);

    //g_signal_connect (session, "on-ssrc-active", (GCallback) on_ssrc_active, media);
    //g_signal_connect (session, "on-sender-ssrc-active", (GCallback) on_sender_ssrc_active, media);
  }
}

static gboolean captureStart(gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //GstElement *pp = (GstElement *)data;
    //gst_element_set_state(pp, GST_STATE_PAUSED);
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    //g_print("%s\n", __FUNCTION__);

    info->file_name = g_strdup_printf("output_%s-ch%d", date_str, info->ch);
    info->captureCnt = 0;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    g_date_time_unref(datetime);
    g_free(date_str);
    return TRUE;
}

static gboolean changeBitrate(gpointer data)
{
    //GstElement *enc = (GstElement *)data;
    CustomData *info = (CustomData *)data;
    gint bitrate;
    g_object_get(info->enc, "bitrate", &bitrate, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d get bitrate : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, bitrate);

    if(bitrate == 4096) g_object_set(info->enc, "bitrate", 1024, NULL);
    if(bitrate == 1024) g_object_set(info->enc, "bitrate", 4096, NULL);

    g_object_get(info->enc, "bitrate", &bitrate, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d set bitrate : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, bitrate);
    
    return TRUE;
}

static gboolean changeFPS(gpointer data)
{
    CustomData *info = (CustomData *)data;
    gint fps;
    g_object_get(info->vr, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d get fps : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, fps);

    if(fps == FILE_FPS) g_object_set(info->vr, "max-rate", RTSP_FPS, NULL);
    if(fps == RTSP_FPS) g_object_set(info->vr, "max-rate", FILE_FPS, NULL);

    g_object_get(info->vr, "max-rate", &fps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d set fps : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, fps);

#if 0
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
#endif
    return TRUE;
}

// appsink의 "new-sample" 시그널 처리 함수
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

    //GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
	//GstClockTime dts = GST_BUFFER_DTS(buffer);
	static GstClockTime pts = 0;
	//GstClockTime dur = GST_BUFFER_DURATION(buffer);

    switch(info->mode)
    {

        case RECORDING:
            {
                GST_BUFFER_PTS(buffer) = pts;
#if 0
                if(pts <= GST_SECOND * 60)
                    pts +=  GST_SECOND / 30;
                else
                    pts = 0;
#endif

                //GST_BUFFER_TIMESTAMP(buffer) = timestamp;
                //GST_BUFFER_DURATION(buffer) = dur;
#ifdef FILENAME_SEC_ZERO
                path = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, fileDateTime, info->ch);
#else
                path = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, info->file_name, info->ch);
#endif
                //path = g_strdup_printf("%s%s.mp4", FILE_PATH, info->file_name);
                guint buffer_size;
                guint8 *data;
                //g_print("path : %s\n", path);
                file = fopen(path, "ab");
                if (file) {
                    fwrite(&pts, sizeof(GstClockTime), 1, file);
                    //fwrite(map.data, 1, map.size, file);
                    if (gst_buffer_extract(buffer, 0, &data, gst_buffer_get_size(buffer)) == TRUE) fwrite(data, 1, gst_buffer_get_size(buffer), path);

                    fclose(file);

                } else {
                    __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
                } 
            }
            break;
        case STREAMING:
            {
                // Create a new buffer and copy data
                info->buf = gst_buffer_new_and_alloc(map.size);
                gst_buffer_fill(info->buf, 0, map.data, map.size);

                // Unmap the original buffer
                //gst_buffer_unmap(buffer, &map);
                //info->buf = newBuffer;
                    
                // Push the new buffer to the appsrc
                //g_thread_new("data-processing-thread", (GThreadFunc)process_data_thread, info);
                gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
            }
            break;
        case CAPTURING:
            {
                if(info->captureCnt >= info->captureMax)
                {
                    //__LOG(LOG_DEBUG, "[GST][%s:%d] captureMax", _FILE_, __LINE__);
                    gst_buffer_unmap(buffer, &map);
                    //gst_sample_unref(sample);
                    return GST_FLOW_OK;
                }
                __LOG(LOG_DEBUG, "[GST][%s:%d] captureCnt %d captureMax %d", _FILE_, __LINE__, info->captureCnt, info->captureMax);

                path = g_strdup_printf("%s%s-%d.jpg", FILE_PATH, info->file_name, info->captureCnt++);
                //path = g_strdup_printf("%s%s_%s_%s-%d-%d.jpg", FILE_PATH, info->ohtName, info->date, info->time, info->ch, info->captureCnt++);
                //path = g_strdup_printf("%s%s_%s-ch%d-%d.jpg", FILE_PATH, ohtName, fileDateTime, info->ch, info->captureCnt++);

                //g_print("path : %s\n", path);
                file = fopen(path, "ab");
                if (file) {
                    fwrite(map.data, 1, map.size, file);
                    fclose(file);
                } else {
                    __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
                }
            }
            break;

        default:
            __LOG(LOG_ERR, "[GST][%s:%d] video mode (%d) unknown ", _FILE_, __LINE__, info->mode);
            break;
    }

    gst_buffer_unmap(buffer, &map);
    //gst_sample_unref(sample);
    if(path!=NULL) g_free(path);

    return GST_FLOW_OK;
}

static gboolean on_samples_selected(GstElement *element, GstSample *sample, gpointer user_data) {

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    return GST_FLOW_OK;
}

static gboolean change_file_datetime() 
{
    //CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    static gint oldMin = -1;
    gint newMin = g_date_time_get_minute(datetime);
    //g_print("oldMin:%d newMin:%d\n", oldMin, newMin);
    if(newMin == oldMin)
        return FALSE;

    oldMin = newMin;
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    
    if(fileDateTime == NULL) 
    {
        fileDateTime = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        fileDateTime = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (fileDateTime : %s)", _FILE_, __LINE__, __FUNCTION__, fileDateTime);
    g_date_time_unref(datetime);

    return TRUE;
}

static gboolean change_file_name(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str;

    //gst_element_send_event(info->mux, gst_event_new_eos());
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    info->file_name = g_strdup_printf("%s_%s-ch%d", ohtName, date_str, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    g_date_time_unref(datetime);
    g_free(date_str);

    return TRUE;
}

static void check_terminal_input(gpointer data) {
    // 여기에서 터미널 입력을 확인하고 처리
    CustomData *customData = (CustomData *)data;
    char input[2];
    //g_print("%s'\n");
    while(1)
    {
        usleep(100000);

        if(is_interrupted)
            break;

        if (fgets(input, sizeof(input), stdin) != NULL) {
            if (input[0] == 'i') {
                visibleSRT(FALSE, customData);
            }
            else if (input[0] == 'v') {
                visibleSRT(TRUE, customData);
            }
            else {
                g_print("You pressed '%c'\n", input[0]);
            }
        }
    }
    return;
    //return TRUE;  // 계속 타이머를 유지
}

static gboolean setSRT(gpointer data) 
{
    CustomData *customData = (CustomData *)data;
    static gint index = 0;
    guint8 i;

    //__LOG(LOG_DEBUG, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_PIPELINE; i++)
    {
#ifdef FILE_L_ENABLE
        g_object_set(customData[FILE0_L+i*MAX_ENC].timeoveraly, "text", g_strdup_printf("test srt num(%d)", index), NULL);
#endif

#ifdef FILE_R_ENABLE
        g_object_set(customData[FILE0_R+i*MAX_ENC].timeoveraly, "text", g_strdup_printf("test srt num(%d)", index), NULL);
#endif

#ifdef RTSP_L_ENABLE
        g_object_set(customData[RTSP0_L+i*MAX_ENC].timeoveraly, "text", g_strdup_printf("test srt num(%d)", index), NULL);
#endif
        
#ifdef RTSP_R_ENABLE
        g_object_set(customData[RTSP0_R+i*MAX_ENC].timeoveraly, "text", g_strdup_printf("test srt num(%d)", index), NULL);
#endif
    }

    index++;

    return TRUE;
}

gboolean visibleSRT(gboolean val, gpointer data) 
{
    CustomData *customData = (CustomData *)data;
    guint8 i;
    gboolean visible = !val;

    __LOG(LOG_DEBUG, "[GST][%s:%d] %s (%d)", _FILE_, __LINE__, __FUNCTION__, visible);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_PIPELINE; i++)
    {
#ifdef FILE_L_ENABLE
        g_object_set(customData[FILE0_L+i*MAX_ENC].timeoveraly, "silent", visible, NULL);
#endif

#ifdef FILE_R_ENABLE
        g_object_set(customData[FILE0_R+i*MAX_ENC].timeoveraly, "silent", visible, NULL);
#endif

#ifdef RTSP_L_ENABLE
        g_object_set(customData[RTSP0_L+i*MAX_ENC].timeoveraly, "silent", visible, NULL);
#endif
        
#ifdef RTSP_R_ENABLE
        g_object_set(customData[RTSP0_R+i*MAX_ENC].timeoveraly, "silent", visible, NULL);
#endif
    }

    return TRUE;
}

static gboolean sendEOS(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);

    //gst_element_send_event(pipeline[info->index], gst_event_new_eos());
    gst_pad_send_event(info->bin_video_pads, gst_event_new_eos());
    gst_pad_send_event(info->bin_audio_pads, gst_event_new_eos());
    //gst_pad_send_event(info->sink_pads, gst_event_new_eos());
    //gst_pad_send_event(info->mux_src_pads, gst_event_new_eos());

    //gst_element_set_state(pipeline[info->index], GST_STATE_READY);
    //gst_element_set_state(pipeline[info->index], GST_STATE_PLAYING);

    return TRUE;
}

static gboolean splitNow(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);

    g_signal_emit_by_name (info->appsink, "split-now");
    //g_signal_emit_by_name (info->appsink, "split-after");
    //g_signal_emit_by_name (info->appsink, "split-at-running-time");

    return TRUE;
}

static gboolean splitAtRunningTime(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);

    g_signal_emit_by_name (info->appsink, "split-now");
    //g_signal_emit_by_name (info->appsink, "split-after");
    //g_signal_emit_by_name (info->appsink, "split-at-running-time");

    return TRUE;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) {
    // Preroll frame 처리 작업 추가 가능
    CustomData *info = (CustomData *)data;
    gchar *sink_name;
    info->appsink = sink;
    //g_print("Preroll frame\n");
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    sink_name = gst_object_get_name(GST_OBJECT(sink));
#if 0
    if(g_str_equal(sink_name, "sink0") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink1") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = FILE_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink2") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = RTSP_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink3") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = RTSP_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink4") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = CAPTURE_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink5") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = CAPTURE_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink6") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE2_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink7") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE2_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink8") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = RTSP2_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink9") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = RTSP2_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink name %s is not matching", _FILE_, __LINE__, sink_name);
        return GST_FLOW_ERROR;
    }
    //info->ch = info->index%2;
#endif
    __LOG(LOG_NOTICE, "[GST][%s:%d] sink_name:%s channel:%d", _FILE_, __LINE__, sink_name, info->ch);
    g_free (sink_name);

#ifndef FILENAME_SEC_ZERO
    if(info->mode == RECORDING)
    {
        change_file_name(info);

#if !defined(MP4_TEST_L) && !defined(MP_TEST_R)
        g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)change_file_name, info);
#endif

    }
#endif

    return GST_FLOW_OK;
}


void sigintHandler(int unused) {
    guint8 i;

	__LOG(LOG_NOTICE, "[GST][%s:%d] sigintHandler", _FILE_, __LINE__);
    //for(i=0; i<MAX_PIPELINE; i++) gst_element_send_event(pipeline[i], gst_event_new_eos());
    destroy();

	return;
}

void sigkillHandler(int unused) {
    guint8 i;

	__LOG(LOG_NOTICE, "[GST][%s:%d] sigkillHandler", _FILE_, __LINE__);
    //for(i=0; i<MAX_PIPELINE; i++) gst_element_send_event(pipeline[i], gst_event_new_eos());
    destroy();

	return;
}

void sigtermHandler(int unused) {
    guint8 i;

	__LOG(LOG_NOTICE, "[GST][%s:%d] sigtermHandler", _FILE_, __LINE__);
    //for(i=0; i<MAX_PIPELINE; i++) gst_element_send_event(pipeline[i], gst_event_new_eos());
    destroy();

	return;
}

void sigHandler(int sig) 
{
    GstState state;
    GstTaskState taskState;
    guint8 i, k;
    static guint8 count = 0;

    if(count++ >= 3)
    {
        destroy();
        return;
    }
    
	__LOG(LOG_NOTICE, "[GST][%s:%d] sigHandler", _FILE_, __LINE__);

    if(sig == SIGINT) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGINT", _FILE_, __LINE__);
    else if(sig == SIGKILL) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGKILL", _FILE_, __LINE__);
    else if(sig == SIGTERM) __LOG(LOG_NOTICE, "[GST][%s:%d] SIGTERM", _FILE_, __LINE__);
    //ipc_clear();
    sigflag = 1;

#if 1
    for(i=0; i<MAX_PIPELINE; i++) 
    {
        //gst_element_send_event(pipeline[i], gst_event_new_eos());
        if (pipeline[i]) {
            for(k=0; k<2; k++)
            {
                if(video_sink_pad[k+i*2]) 
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] i:%d k:%d sink pad eos", _FILE_, __LINE__,i,k);
                    gst_pad_send_event(video_sink_pad[k+i*2], gst_event_new_eos());
                    gst_pad_send_event(audio_sink_pad[k+i*2], gst_event_new_eos());
                }
                else __LOG(LOG_NOTICE, "[GST][%s:%d] i:%d k:%d sink pad NULL", _FILE_, __LINE__, i, k);
                //__LOG(LOG_NOTICE, "[GST][%s:%d] i:%d k:%d", _FILE_, __LINE__,i,k);
                //gst_pad_send_event(audio_sink_pad[k+i*2], gst_event_new_eos());
            }
            //__LOG(LOG_NOTICE, "[GST][%s:%d] i:%d k:%d", _FILE_, __LINE__,i,k);
            //gst_bus_poll(bus[i], GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);
            //gst_object_unref(bus[i]);
        }
    }
#endif

    destroy();

	return;
}


void attachInterruptHandlers()
{
    //signal(SIGUSR1, ipcHandler);
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTERM, sigHandler);
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
    appsrc_name = g_strdup_printf("%s%d", APPSRC_NAME, info->index);
    __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	info->appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), appsrc_name);

    //queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	//queue = gst_bin_get_by_name_recurse_up(GST_BIN(element), queue_name);

    //g_object_set (G_OBJECT (queue), 

    g_free(appsrc_name);
    //g_free(queue_name);
	gst_object_unref(element);

    g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
	//g_mutex_unlock(&mutex);
    return;
}

static gboolean cleanRtspSession(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;

   //__LOG(LOG_DEBUG, "[GST][%s:%d] rtsp session pool", _FILE_, __LINE__);

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}


static void pad_added_handler (GstElement * src, GstPad * new_pad, gpointer data)
{
  GstElement *sink = (GstElement*)data;
  GstPad *sink_pad = gst_element_get_static_pad (sink, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
#if 0
  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);

  gchar *caps_str = gst_caps_to_string(new_pad_caps);
  __LOG(LOG_NOTICE, "[GST][%s:%d] Current caps: %s", _FILE_, __LINE__, caps_str);
  g_free(caps_str);
  
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    g_print ("It has type '%s' which is not raw audio. Ignoring.\n",
        new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
#endif
  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}

static void pad_removed_handler (GstElement * src, GstPad * new_pad, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    return;
}

static void client_closed(GstRTSPClient* client, gpointer user_data)
{
	//CustomData *info = (CustomData*)user_data;
    //gchar *client_ip = (gchar *)user_data;
	const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP2 : %s", _FILE_, __LINE__, client_ip_2);

    //if(client_ip)g_free(client_ip);
}

static gboolean handle_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) 
{
    //CustomData *info = (CustomData*)user_data;
    // This function is called when a new client is connected to the RTSP server.
    // You can handle the client connection here.
    const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));
    //GstRTSPUrl *url = gst_rtsp_connection_get_url(gst_rtsp_client_get_connection(client));
    //GstRTSPMountPoints *mount = gst_rtsp_client_get_mount_points(client);
    //GstRTSPContext  *path = gst_rtsp_client_get_rtsp_context(client);
    //GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->host);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->abspath);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %d", _FILE_, __LINE__, url->port);

    g_signal_connect(client, "closed", (GCallback)(client_closed), NULL);

    //g_free(client_ip);
    // Initialize session for the new client
    
    return TRUE;
}

static gchararray get_filename(gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s_%s-ch%d.mp4", ohtName, date_str, info->ch);

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

static gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //gint sec = g_date_time_get_second(datetime);
    gchar *date_str;

#if 1
    if(info->firstSplitFlag == 0)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d firstSplitFlag", _FILE_, __LINE__, info->ch);
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, data);
        info->firstSplitFlag = 1;
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        if(info->is_live)
            date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        else
            date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
#endif

    //date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, date_str, info->ch);
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

void sink_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] sink_added", _FILE_, __LINE__);

    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "appsink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "appsink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_message("sink name err");
    }

    return;
}


void muxer_added(GstElement *sink, guint arg0, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] muxer_added", _FILE_, __LINE__);

    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "appsink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "appsink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_message("sink name err");
    }

    return;
}

gint setRtspPipe(gpointer user_data)
{
  //GgstLoop *loop;
  //GstRTSPServer *server;
  //GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  CustomData *info = (CustomData*)user_data;
  gchar *srcName = g_strdup_printf("%s%d", APPSRC_NAME, info->index);

  info->appsrc = gst_element_factory_make("appsrc", srcName);

  //gst_init (&argc, &argv);
  //g_print("info.ch:%d info.filename:%d\n", info->ch, info->file_name); 
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);

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
  
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] appsrc name : %s", _FILE_, __LINE__, srcName);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s is-live=1 ! queue ! h264parse \
                    ! rtph264pay name=pay0 config-interval=-1 )", info->appsrc_name);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s%d is-live=1 ! queue max-size-time=0 max-size-buffers=1 ! h264parse ! rtph264pay name=pay0 )", APPSRC_NAME, info->ch);
  gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 ! queue max-size-buffers=60 max-size-time=1000000000 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", srcName);
  //gchar *launch_str = g_strdup_printf("( appsrc name=%s max-bytes=0 do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=10 leaky=2 \
			! h264parse ! rtph264pay name=pay0 config-interval=-1 )", info->appsrc_name);
  gst_rtsp_media_factory_set_launch(factory, launch_str);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_free(launch_str);

  //gst_rtsp_media_factory_set_launch(factory1, launch_str);
  //gst_rtsp_media_factory_set_shared(factory1, TRUE);
  /* notify when our media is ready, This is called whenever someone asks for
   * the media and a new pipeline with our appsrc is created */
  g_signal_connect (factory, "media-configure", (GCallback) media_configure, info);
  //g_signal_connect (factory, "client-connected", (GCallback) handle_client_connected, info);
  //g_signal_connect (factory, "media-constructed", (GCallback) media_constructed, info);
  //g_signal_connect(factory, "pad-added", G_CALLBACK(pad_added_handler), NULL);
  //g_signal_connect(factory, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);
#if 1
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] suspend_mode : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_suspend_mode(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] protocol : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_protocols(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] porfile : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_profiles(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] buffer size : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_buffer_size(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] retransmisson time : %" GST_TIME_FORMAT "", _FILE_, __LINE__, gst_rtsp_media_factory_get_retransmission_time(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] do retransmisson  : %s", _FILE_, __LINE__, gst_rtsp_media_factory_get_do_retransmission(factory)? "TRUE":"FALSE");
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] latency  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_latency(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] transport mode  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_transport_mode(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] media type  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_media_gtype(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] clock : %" GST_TIME_FORMAT "", _FILE_, __LINE__, gst_rtsp_media_factory_get_clock(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] publish clock mode : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_publish_clock_mode(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] max mcast ttl : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_max_mcast_ttl(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] bind mcast address : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_bind_mcast_address(factory)? "TRUE":"FALSE");
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] dscp qos : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_dscp_qos(factory));
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] eos shutdown : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_eos_shutdown(factory)? "TRUE":"FALSE");
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] stop on disconnect : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_stop_on_disonnect(factory)? "TRUE":"FALSE");
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] shared : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_shared(factory)? "TRUE":"FALSE");
  //gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_PAUSE);
#endif
  /* attach the test factory to the /test url */
  gchar *point = g_strdup_printf("/ch%d", info->ch);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

  gst_rtsp_mount_points_add_factory (rtspMounts, point, factory);

  gst_rtsp_media_factory_add_role (factory, "semes",
      GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
      GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

#if 0
  GstRTSPUrl *url;
  GstRTSPMedia *media;
  GstElement *pipelineRtsp;

  if(gst_rtsp_url_parse ("rtsp://localhost:8554/ch0", &url) != GST_RTSP_OK) {
    g_error("gst_rtsp_url_parse failed");;
  }

  media = gst_rtsp_media_factory_construct (factory, url);
  if(!GST_IS_RTSP_MEDIA (media)){
    g_error("GST_IS_RTSP_MEDIA failed");
  }

  pipelineRtsp = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipelineRtsp));

    GstStateChangeReturn ret = gst_element_set_state(pipelineRtsp, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
        gst_object_unref(pipelineRtsp);
        return -1;
    }
#endif

#if 0
  GstRTSPUrl *url; 

  if(gst_rtsp_url_parse ("rtsp://localhost:8554/ch0", &url) != GST_RTSP_OK) {
    g_error("gst_rtsp_url_parse failed");
  }
  GstElement *pipelineRtsp = gst_rtsp_media_factory_create_element (factory, url);

  g_timeout_add_seconds (10, (GSourceFunc) captureStart, pipelineRtsp);
#endif

  //gst_rtsp_mount_points_add_factory (mounts, "/ch1", factory1);

  /* don't need the ref to the mounts anymore */
  //g_object_unref (mounts);

  /* attach the server to the default maincontext */
  //if (gst_rtsp_server_attach (server, NULL) == 0) goto failed;

  /* start serving */
  //g_print ("stream ready at rtsp://127.0.0.1:8554/ch0\n");
  __LOG(LOG_NOTICE, "[RTSP][%s:%d]stream ready at rtsp://127.0.0.1:%s%s", _FILE_, __LINE__, RTSP_PORT, point);
  //GgstLoop *loop = g_main_loop_new (NULL, FALSE);
  //g_main_loop_run (loop);

  //__LOG(LOG_CRIT, "[GST][%s:%d] thread exit", _FILE_, __LINE__);
  //g_main_loop_unref(loop);
  //g_object_unref(server);
  //g_object_unref(factory);
  g_free(point);
  g_free(srcName);

  return GST_FLOW_OK;

failed:
  {
    __LOG(LOG_CRIT, "[RTSP][%s:%d] failed to attach the server", _FILE_, __LINE__);
    return -1;
  }
}

static gboolean eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    CustomData *info = (CustomData *)user_data;

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s (ch:%d)", _FILE_, __LINE__, __FUNCTION__, info->ch);
    //gst_element_set_state(info->mux, GST_STATE_PAUSED);
#if 0
    gst_element_get_state(info->mux, NULL, NULL, GST_CLOCK_TIME_NONE);
    GstStateChangeReturn result = gst_element_get_state(info->mux, NULL, NULL, GST_CLOCK_TIME_NONE);

    // Check the result
    if (result == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to change state to PAUSED.\n");
        // Handle the failure case if needed
    } else if (result == GST_STATE_CHANGE_ASYNC) {
        g_print("State change is still asynchronous.\n");
        // Handle the asynchronous case if needed
    } else if (result == GST_STATE_CHANGE_SUCCESS) {
        g_print("State change to PAUSED completed.\n");
        // Continue with the rest of the code
    }
    
#endif
    GstState state;
    gst_element_get_state(pipeline[info->ch/2], &state, NULL, GST_CLOCK_TIME_NONE);
    g_message("to : %s", gst_element_state_get_name(state));
    //gst_element_set_state(appsink, GST_STATE_NULL);
    //gst_element_set_state(appsink, GST_STATE_PLAYING);
    //gst_element_set_state(pipeline[info->ch/2], GST_STATE_NULL);
    //gst_element_set_state(pipeline[info->ch/2], GST_STATE_NULL);
    //change_file_name(user_data);
    //gst_element_set_state(pipeline[info->ch/2], GST_STATE_PLAYING);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s end", _FILE_, __LINE__, __FUNCTION__);
    //change_file_datetime(NULL);
    return TRUE;
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

void cleanup() {
    //g_message("Cleaning up GStreamer...");
    __LOG(LOG_CRIT, "[GST][%s:%d] Cleaning up GStreamer", _FILE_, __LINE__);
    gst_deinit();  // GStreamer 해제
}

int rtsp_server_start()
{
    rtspServer = gst_rtsp_server_new ();
    g_object_set (rtspServer, "service", RTSP_PORT, NULL);

    rtspMounts = gst_rtsp_server_get_mount_points (rtspServer);

    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);

    g_signal_connect(rtspServer, "client-connected", G_CALLBACK(handle_client_connected), NULL);
    
    //g_signal_connect(rtspServer, "client-disconnected", G_CALLBACK(handle_client_disconnected), NULL);

    

#ifdef RTSP_AUTH_ENABLE
    /* make a new authentication manager */
    GstRTSPAuth *auth = gst_rtsp_auth_new ();

    /* make user token */
    GstRTSPToken *token =
        gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
        "semes", NULL);
    gchar *basic = gst_rtsp_auth_make_basic ("semes", "semes");
    gst_rtsp_auth_add_basic (auth, basic, token);
    g_free (basic);
    gst_rtsp_token_unref (token);
    gst_rtsp_server_set_auth (rtspServer, auth);
    g_object_unref (auth);
    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);
#endif

    if (gst_rtsp_server_attach (rtspServer, NULL) == 0) {
        __LOG(LOG_CRIT, "[GST][%s:%d] rtsp server attach failed", _FILE_, __LINE__);
        return -1;
    }
}

#include <sys/ipc.h>
#define MSG_Q_KEY	(0x64)
enum MSG_TYPE {
  PMSG_TYPE_UNUSED = 0,
  PMSG_TYPE_IPC
};

typedef struct _RecvBuf
{
    glong type;
    guint8 data[16];
} RecvBuf;

int ipc_init()
{
    gint ret = 0;
    gint msg_id;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

    if(msg_id == -1) {
        perror("msgget fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return msg_id;
    }
	ret = msgctl(msg_id, IPC_RMID, NULL);
    if(ret < 0) {
		 perror("msgctl fail");
		 __LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}
    if(ret < 0)
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);

    return ret;
}

int ipc_clear()
{
    gint ret = 0;
    void* nStatus;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

	if(ret < 0)
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);

    int msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

    if(msg_id == -1) {
        perror("msgget fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return msg_id;
    }

	ret = msgctl(msg_id, IPC_RMID, NULL);
    if(ret < 0) {
		perror("msgctl fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}

    g_thread_join(ipcThread);

    return ret;
}

int parseIpcRecvData(int msgId, char* data, int len)
{

}

static void ipcLoop(CustomData* data)
{
    RecvBuf recvbuf;
    gint msg_id;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    while(1) 
    {
        usleep(1000);

        if(is_interrupted)
            break;

        msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);
        if(msg_id == -1) {
            perror("msgget fail");
            __LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, msg_id);
            return;
        }

        int ret = msgrcv(msg_id, &recvbuf, sizeof(recvbuf) - sizeof(long), PMSG_TYPE_IPC, 0);

        if(ret <= 0) {
            perror("msgrcv fail");
            __LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
            return;
        }
        __LOG(LOG_INFO, "[IPC][%s:%d] recv data msg_id(%d) byte %d", _FILE_, __LINE__, msg_id, ret);

        //for(guint8 i=0; i<16; i++) __LOG(LOG_INFO, "[IPC][%s:%d] data[%d] : 0x%02x", _FILE_, __LINE__, i, recvbuf.data[i]);

        if(recvbuf.data[11] == 'c')
        {
            captureStart(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
        else if(recvbuf.data[11] == 'b')
        {
            changeBitrate(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
        else if(recvbuf.data[11] == 'f')
        {
            changeFPS(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
    }

    return;
}

static void splitTimerStart(gpointer data, gint startSec)
{
    CustomData *customData = (CustomData *)data;
    static gboolean start_flag = 0;
    static gint staticMin = -1;

    if(start_flag == 1) return;
    
    GDateTime *datetime = g_date_time_new_now_local();
    gint min = g_date_time_get_minute(datetime);
    gint sec = g_date_time_get_second(datetime);
    gint microsec = g_date_time_get_microsecond(datetime);
    gint i;

    //g_print("sec:%d microsec:%d\n", sec, microsec);
    //__LOG(LOG_DEBUG, "[GST][%s:%d] sec:%d microsec:%d", _FILE_, __LINE__, sec, microsec);

    //if(sec == 0 && microsec <= 10000)
    if(staticMin == min) {
        g_date_time_unref(datetime);
        return;
    }

    if(sec == 1)    //if(sec == 1 && microsec <= 50000)   //if(sec == 59 && microsec >= 700000 && microsec <= 750000)
    {
        for(i=0; i<MAX_PIPELINE; i++)
        {
#ifdef FILE_L_ENABLE
            if(customData[FILE0_L+i*MAX_ENC].firstSplitFlag == 1)
            {
                splitNow(&customData[FILE0_L+i*MAX_ENC]);
                g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, &customData[FILE0_L+i*MAX_ENC]);
            }
            else
                break;
#endif

#ifdef FILE_R_ENABLE
            if(customData[FILE0_R+i*MAX_ENC].firstSplitFlag == 1)
            {
                splitNow(&customData[FILE0_R+i*MAX_ENC]);
                g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, &customData[FILE0_R+i*MAX_ENC]);
            }
            else
                break;
#endif
            staticMin = min;
            start_flag = 1;
            __LOG(LOG_NOTICE, "[GST][%s:%d] %s : split start", _FILE_, __LINE__, __FUNCTION__);
        }
        
    }
    g_date_time_unref(datetime);

    return;
}

static gboolean handle_eos_event(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
    CustomData *data = (CustomData *)user_data;
    static guint8 count = 0;
    GstEventType event = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info));
    const gchar *eventName = gst_event_type_get_name(event);
    
    // Print the event name


    if (event == GST_EVENT_EOS) {
        g_print("Received EOS event on pad: %s\n", GST_PAD_NAME(pad));
        // Add your EOS event handling code here
        __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch[%d] count[%d]", _FILE_, __LINE__, __FUNCTION__, data->ch, count);
        data->is_live = FALSE;
        //splitNow(data);
        //if(++count > 1) destroy();
    }
    else if(event == GST_EVENT_TAG)
    {

    }
    else
    {
        g_print("Event Type: %s\n", eventName);
    }

    return GST_PAD_PROBE_OK;
}

static void taskLoop(gpointer data)
{
    CustomData *customData = (CustomData *)data;
#ifdef FILENAME_SEC_ZERO
    //change_file_datetime();
#endif

    splitTimerStart(customData, 0);
#if 0
    if (g_io_channel_unix_get_fd(g_io_channel_unix_new(fileno(stdin))) != -1) {
        char input[2];
        if (fgets(input, sizeof(input), stdin) != NULL) {
            if (input[0] == 'i') {
                visibleSRT(FALSE, customData);
            }
            if (input[0] == 'v') {
                visibleSRT(TRUE, customData);
            }
        }
    }
#endif
    usleep(1000);

    return;
}

int main(int argc, char *argv[]) {

    gst_init(&argc, &argv);
    atexit(cleanup);
    attachInterruptHandlers();

    ipc_init();

    PipeMain main[MAX_PIPELINE];
    CustomData customData[MAX_ENC*MAX_PIPELINE];
    guint8 i = 0, idx = 0, tdx = 0;
    GstCaps *caps;
    GstStateChangeReturn ret;
    GstState state;
    ohtName = g_strdup_printf("%s", "output");

#if defined(RTSP_L_ENABLE) || defined(RTSP_R_ENABLE)
    rtsp_server_start();
    guint cleanRtsp_id = g_timeout_add_seconds (5, (GSourceFunc) cleanRtspSession, rtspServer);
#endif

    // 파이프라인 생성

    //i = 1;

    do
    {
        pipeline[i] = NULL;
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipeline(%d) create", _FILE_, __LINE__, i);
        pipeline[i] = gst_pipeline_new(g_strdup_printf("pipeline%d", i));
#if 1
        // 요소 생성
        main[i].src = gst_element_factory_make("v4l2src", "src");
        //GstElement *src = gst_element_factory_make("videotestsrc", "src");
        main[i].convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
        main[i].capsfilter = gst_element_factory_make("capsfilter", "caps");
        main[i].tee = gst_element_factory_make("tee", "tee");

        g_object_set(main[i].src, "do-timestamp", TRUE, NULL);

        if (!pipeline[i] || !main[i].src || !main[i].capsfilter || !main[i].tee)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Main pipe create error", _FILE_, __LINE__);
        }

        // 요소가 생성되지 않은 경우 에러 처리

        // 파이프라인에 요소 추가
        gst_bin_add_many(GST_BIN(pipeline[i]), main[i].src, main[i].capsfilter, main[i].convert, main[i].tee, NULL);

        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipe link start", _FILE_, __LINE__);
        // 요소 연결
        if (!gst_element_link_many(main[i].src, main[i].capsfilter, main[i].convert, main[i].tee, NULL)) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, i);
            gst_object_unref(pipeline[i]);
            return -1;
        }
#endif

#ifdef AUDIO_TEST
        do
        {
            GstElement *audioSrc = gst_element_factory_make("audiotestsrc", "audio-source");
            GstElement *audioConvert = gst_element_factory_make("audioconvert", "audio-convert");
            GstElement *audioResample = gst_element_factory_make("audioresample", "audio-resample");
            GstElement *audioEncode = gst_element_factory_make("lamemp3enc", "audio-encode");
            GstElement *audioParse = gst_element_factory_make("mpegaudioparse", "audio-parse");
            GstElement *mux = gst_element_factory_make("mp4mux", "audio-mux");
            GstElement *filesink = gst_element_factory_make("filesink", "audio-sink");
            
            if (!audioSrc || !audioConvert || !audioResample || !audioEncode || !audioParse || !mux || !filesink )
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            g_object_set(filesink, "location", "test.mp3", NULL);
            gst_bin_add_many(GST_BIN(pipeline[i]), audioSrc, audioConvert, audioResample, audioEncode, audioParse, mux, filesink, NULL);
            if (!gst_element_link_many(audioSrc, audioConvert, audioResample, audioEncode, filesink, NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

        } while (0);
        
#endif
        //g_signal_connect(src, "pad-added", G_CALLBACK(pad_added_handler), NULL);
        //g_signal_connect(src, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);

#ifdef MP4_TEST_APPSINK
        do
        {
            idx = FILE0_L;
            tdx = idx+i*MAX_ENC;
            __LOG(LOG_NOTICE, "[GST][%s:%d] videoPipe pipe(%d) create start", _FILE_, __LINE__, tdx);

            main[i].videoPipe[idx].videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", tdx));
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].queue2 = gst_element_factory_make("queue2", g_strdup_printf("queue2_%d", tdx));
            main[i].videoPipe[idx].parse = gst_element_factory_make("h264parse", g_strdup_printf("parse%d", tdx));
            main[i].videoPipe[idx].mux = gst_element_factory_make("mp4mux", g_strdup_printf("mux%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("filesink", g_strdup_printf("appsink%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("appsink%d", tdx));

            if (!main[i].videoPipe[idx].videorate || !main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].encoder || !main[i].videoPipe[idx].queue \
                || !main[i].videoPipe[idx].mux || !main[i].videoPipe[idx].parse || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] make error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            printf("10\n");
            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].videorate, \
                    main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].videorate, \
                    main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            g_object_set(main[i].videoPipe[idx].mux, "faststart", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].mux, "emit-signals", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].mux, "trak-timescale", 30, NULL);
            g_signal_connect(main[i].videoPipe[idx].mux, "samples-selected", G_CALLBACK(on_samples_selected), NULL);

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].videorate, "max-rate", FILE_FPS, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "drop-only", FALSE, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", 1, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "avg-in-rate", FILE_BITRATE, NULL);
            g_object_set(main[i].videoPipe[idx].encoder, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", FALSE, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "eos", G_CALLBACK(eos_callback), &customData[tdx]);
            //g_object_set(main[i].videoPipe[idx].sink, "max-size-time", (FILE_SAVE_DURATION*GST_SECOND), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            //g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(format_location), &customData[tdx]);

            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = RECORDING;
            customData[tdx].file_name = NULL;
            printf("11\n");
            main[i].videoPipe[idx].audioPipe.src = gst_element_factory_make("audiotestsrc", "audio-source");
            main[i].videoPipe[idx].audioPipe.convert = gst_element_factory_make("audioconvert", "audio-convert");
            main[i].videoPipe[idx].audioPipe.resample = gst_element_factory_make("audioresample", "audio-resample");
            main[i].videoPipe[idx].audioPipe.encoder = gst_element_factory_make("lamemp3enc", "audio-encode");
            main[i].videoPipe[idx].audioPipe.parse = gst_element_factory_make("mpegaudioparse", "audio-parse");
            main[i].videoPipe[idx].audioPipe.queue = gst_element_factory_make("queue2", "audio-queue");
            if (!main[i].videoPipe[idx].audioPipe.src || !main[i].videoPipe[idx].audioPipe.convert || !main[i].videoPipe[idx].audioPipe.resample \
                    || !main[i].videoPipe[idx].audioPipe.encoder || !main[i].videoPipe[idx].audioPipe.parse || !main[i].videoPipe[idx].audioPipe.queue)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            printf("12\n");
            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.resample, \
                        main[i].videoPipe[idx].audioPipe.encoder, main[i].videoPipe[idx].audioPipe.parse, NULL);
            if (!gst_element_link_many(main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.resample, \
                        main[i].videoPipe[idx].audioPipe.encoder, NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            printf("13\n");
            customData[tdx].mux = main[i].videoPipe[idx].mux;
            customData[tdx].mux = main[i].videoPipe[idx].sink;
            printf("1\n");
            main[i].videoPipe[idx].bins = gst_bin_new ("bin0");
            gst_bin_add_many(GST_BIN(main[i].videoPipe[idx].bins), main[i].videoPipe[idx].mux, main[i].videoPipe[idx].sink, NULL);
#if 1
            if (!gst_element_link_many(main[i].videoPipe[idx].mux, main[i].videoPipe[idx].sink, NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] bin[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
#else
            if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].mux, "src"), gst_element_get_static_pad(main[i].videoPipe[idx].sink, "sink")) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] sink pad link error", _FILE_, __LINE__);
                break;
            }
#endif

            gst_bin_add(GST_BIN(pipeline[i]), main[i].videoPipe[idx].bins);
            printf("2\n");
            gst_element_add_pad(main[i].videoPipe[idx].bins, gst_ghost_pad_new("sink_video", gst_element_get_request_pad(main[i].videoPipe[idx].mux, "video_%u")));
            printf("3\n");
            customData[tdx].bin_video_pads = gst_element_get_static_pad(main[i].videoPipe[idx].bins, "sink_video");
            printf("4\n");
            if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].parse, "src"), customData[tdx].bin_video_pads) != GST_PAD_LINK_OK)
            {
                //g_printerr("Tee could not be linked(crop_pad, queue_crop_pad)\n");
                __LOG(LOG_CRIT, "[GST][%s:%d] video pad link error", _FILE_, __LINE__);
                break;
            }
            printf("5\n");
            gst_element_add_pad(main[i].videoPipe[idx].bins, gst_ghost_pad_new("sink_audio", gst_element_get_request_pad(main[i].videoPipe[idx].mux, "audio_%u")));
            printf("6\n");
            customData[tdx].bin_audio_pads = gst_element_get_static_pad(main[i].videoPipe[idx].bins, "sink_audio");
            printf("7\n");
            if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].audioPipe.encoder, "src"), customData[tdx].bin_audio_pads) != GST_PAD_LINK_OK)
            {
                //g_printerr("Tee could not be linked(crop_pad, queue_crop_pad)\n");
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pad link error", _FILE_, __LINE__);
                break;
            }
            printf("8\n");


            g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)sendEOS, &customData[tdx]);

        } while(0);
#endif

#ifdef FILE_L_ENABLE
        do
        {
            idx = FILE0_L;
            tdx = idx+i*MAX_ENC;

            main[i].videoPipe[idx].videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", tdx));
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].queue2 = gst_element_factory_make("queue2", g_strdup_printf("queue2_%d", tdx));
            main[i].videoPipe[idx].parse = gst_element_factory_make("h264parse", g_strdup_printf("parse%d", tdx));
            main[i].videoPipe[idx].mux = gst_element_factory_make("mp4mux", g_strdup_printf("mux%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("filesink", g_strdup_printf("appsink%d", tdx));
            main[i].videoPipe[idx].timeoverlay = gst_element_factory_make("timeoverlay", g_strdup_printf("timeoverlay%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", tdx));

            if (!main[i].videoPipe[idx].videorate || !main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].timeoverlay || !main[i].videoPipe[idx].encoder \
                || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].mux || !main[i].videoPipe[idx].parse || !main[i].videoPipe[idx].queue2 || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] make error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].timeoverlay, main[i].videoPipe[idx].queue, \
                    main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, main[i].videoPipe[idx].mux, \
                    main[i].videoPipe[idx].queue2, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].timeoverlay, main[i].videoPipe[idx].queue, \
                    main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            //g_object_set(main[i].videoPipe[idx].mux, "faststart", TRUE, NULL);

            g_object_set(main[i].videoPipe[idx].timeoverlay, "valignment", 2, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "halignment", 0, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Times New Roman Italic, 16", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "show-times-as-dates", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-epoch", g_date_time_new_now_local(), NULL);
            
            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].parse, "config-interval", -1, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "max-rate", FILE_FPS, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "drop-only", FALSE, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "max-size-bytes", 0, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, "leaky", 1, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "avg-in-rate", FILE_BITRATE, NULL);
            g_object_set(main[i].videoPipe[idx].encoder, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", FALSE, NULL);
            //g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            //g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            //g_signal_connect(main[i].videoPipe[idx].sink, "eos", G_CALLBACK(eos_callback), &customData[tdx]);
            //g_object_set(main[i].videoPipe[idx].sink, "max-size-time", (FILE_SAVE_DURATION*GST_SECOND), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "muxer", main[i].videoPipe[idx].mux, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", "tost%05d.mp4", NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "max-size-time", FILE_SAVE_DURATION, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "async-finalize", TRUE, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "async-handling", TRUE, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "format-location", G_CALLBACK(format_location), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "sink-added", G_CALLBACK(sink_added), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "muxer-added", G_CALLBACK(muxer_added), &customData[tdx]);

            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].timeoveraly = main[i].videoPipe[idx].timeoverlay;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = RECORDING;
            customData[tdx].file_name = NULL;
            customData[tdx].appsink = main[i].videoPipe[idx].sink;
            customData[tdx].firstSplitFlag = 0;
            customData[tdx].is_live = TRUE;
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("%s", get_filename(&customData[tdx])), NULL);
            video_sink_pad[customData[tdx].ch] = gst_element_get_request_pad(main[i].videoPipe[idx].sink, "video_aux_%u");
            gst_pad_add_probe(video_sink_pad[customData[tdx].ch], GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, handle_eos_event, &customData[tdx], NULL);
            if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].parse, "src"), video_sink_pad[customData[tdx].ch]) != GST_PAD_LINK_OK)
            {
                g_message("video pad[%d] link err", customData[tdx].ch);
                return -1;
            }
#ifdef AUDIO_L_ENABLE
            main[i].videoPipe[idx].audioPipe.src = gst_element_factory_make("audiotestsrc", g_strdup_printf("audio-src%d", tdx));
            main[i].videoPipe[idx].audioPipe.convert = gst_element_factory_make("audioconvert", g_strdup_printf("audio-convert%d", tdx));
            main[i].videoPipe[idx].audioPipe.resample = gst_element_factory_make("audioresample", g_strdup_printf("audio-resample%d", tdx));
            main[i].videoPipe[idx].audioPipe.encoder = gst_element_factory_make("lamemp3enc", g_strdup_printf("audio-enc%d", tdx));
            main[i].videoPipe[idx].audioPipe.parse = gst_element_factory_make("mpegaudioparse", g_strdup_printf("audio-parse%d", tdx));
            main[i].videoPipe[idx].audioPipe.queue = gst_element_factory_make("queue", g_strdup_printf("audio-queue%d", tdx));
            main[i].videoPipe[idx].audioPipe.tee = gst_element_factory_make("tee", g_strdup_printf("audio-tee%d", tdx));
            if (!main[i].videoPipe[idx].audioPipe.src || !main[i].videoPipe[idx].audioPipe.convert || !main[i].videoPipe[idx].audioPipe.resample \
                    || !main[i].videoPipe[idx].audioPipe.encoder || !main[i].videoPipe[idx].audioPipe.parse || !main[i].videoPipe[idx].audioPipe.queue || !main[i].videoPipe[idx].audioPipe.tee)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            g_object_set(main[i].videoPipe[idx].audioPipe.src, "is-live", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.src, "wave", 8, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.src, "tick-interval", 200000000, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.src, "apply-tick-ramp", TRUE, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-bytes", 0, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-buffers", 60, "leaky", 1, NULL);

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.resample, \
                        main[i].videoPipe[idx].audioPipe.encoder, main[i].videoPipe[idx].audioPipe.parse, main[i].videoPipe[idx].audioPipe.queue, main[i].videoPipe[idx].audioPipe.tee, NULL);
            if (!gst_element_link_many(main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.encoder, \
                        main[i].videoPipe[idx].audioPipe.queue, main[i].videoPipe[idx].audioPipe.parse, main[i].videoPipe[idx].audioPipe.tee, NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            //if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].audioPipe.tee, "src"), \
                            gst_element_get_request_pad(main[i].videoPipe[idx].sink, "audio_%u")) != GST_PAD_LINK_OK)
            //customData[tdx].bin_audio_pads = gst_element_get_request_pad(main[i].videoPipe[idx].sink, "audio_%u");
            audio_sink_pad[customData[tdx].ch] = gst_element_get_request_pad(main[i].videoPipe[idx].sink, "audio_%u");
            if(gst_pad_link(gst_element_get_request_pad(main[i].videoPipe[idx].audioPipe.tee, "src_%u"), \
                            audio_sink_pad[customData[tdx].ch]) != GST_PAD_LINK_OK)
            {
                g_message("audio pad link err");
                return -1;
            }

#endif
            //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)sendEOS, &customData[tdx]);
            //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, &customData[tdx]);
        } while(0);
#endif

#ifdef FILE_R_ENABLE
        do
        {
            idx = FILE0_R;
            tdx = idx+i*MAX_ENC;

            main[i].videoPipe[idx].videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", tdx));
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].queue2 = gst_element_factory_make("queue2", g_strdup_printf("queue2_%d", tdx));
            main[i].videoPipe[idx].parse = gst_element_factory_make("h264parse", g_strdup_printf("parse%d", tdx));
            main[i].videoPipe[idx].mux = gst_element_factory_make("mp4mux", g_strdup_printf("mux%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));
            //main[i].videoPipe[idx].sink = gst_element_factory_make("filesink", g_strdup_printf("appsink%d", tdx));
            main[i].videoPipe[idx].timeoverlay = gst_element_factory_make("timeoverlay", g_strdup_printf("timeoverlay%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", tdx));

            if (!main[i].videoPipe[idx].videorate || !main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].timeoverlay || !main[i].videoPipe[idx].encoder \
                || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].mux || !main[i].videoPipe[idx].parse || !main[i].videoPipe[idx].queue2 || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] make error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].timeoverlay, main[i].videoPipe[idx].queue, \
                    main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, main[i].videoPipe[idx].mux, \
                    main[i].videoPipe[idx].queue2, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].timeoverlay, main[i].videoPipe[idx].queue, \
                    main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].parse, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            //g_object_set(main[i].videoPipe[idx].mux, "faststart", TRUE, NULL);

            g_object_set(main[i].videoPipe[idx].timeoverlay, "valignment", 2, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "halignment", 0, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Times New Roman Italic, 16", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "show-times-as-dates", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-epoch", g_date_time_new_now_local(), NULL);

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].videorate, "max-rate", FILE_FPS, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "drop-only", FALSE, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "max-size-bytes", 0, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, "leaky", 1, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].queue2, "avg-in-rate", FILE_BITRATE, NULL);
            g_object_set(main[i].videoPipe[idx].encoder, "bitrate", FILE_BITRATE, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", FALSE, NULL);
            //g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            //g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            //g_signal_connect(main[i].videoPipe[idx].sink, "eos", G_CALLBACK(eos_callback), &customData[tdx]);
            //g_object_set(main[i].videoPipe[idx].sink, "max-size-time", (FILE_SAVE_DURATION*GST_SECOND), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "muxer", main[i].videoPipe[idx].mux, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", "test%05d.mp4", NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "max-size-time", FILE_SAVE_DURATION, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("test%d.mp4", i), NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "async-finalize", TRUE, NULL);
            //g_object_set(main[i].videoPipe[idx].sink, "async-handling", TRUE, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "format-location", G_CALLBACK(format_location), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "sink-added", G_CALLBACK(sink_added), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "muxer-added", G_CALLBACK(muxer_added), &customData[tdx]);

            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].timeoveraly = main[i].videoPipe[idx].timeoverlay;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = RECORDING;
            customData[tdx].file_name = NULL;
            customData[tdx].appsink = main[i].videoPipe[idx].sink;
            customData[tdx].firstSplitFlag = 0;
            customData[tdx].is_live = TRUE;
            //g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("%s", get_filename(&customData[tdx])), NULL);
            video_sink_pad[customData[tdx].ch] = gst_element_get_request_pad(main[i].videoPipe[idx].sink, "video_aux_%u");
            gst_pad_add_probe(video_sink_pad[customData[tdx].ch], GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, handle_eos_event, &customData[tdx], NULL);
            if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].parse, "src"), video_sink_pad[customData[tdx].ch]) != GST_PAD_LINK_OK)
            {
                g_message("video pad[%d] link err", customData[tdx].ch);
                return -1;
            }
#ifdef AUDIO_R_ENABLE
            main[i].videoPipe[idx].audioPipe.src = gst_element_factory_make("audiotestsrc", g_strdup_printf("audio-src%d", tdx));
            main[i].videoPipe[idx].audioPipe.convert = gst_element_factory_make("audioconvert", g_strdup_printf("audio-convert%d", tdx));
            main[i].videoPipe[idx].audioPipe.resample = gst_element_factory_make("audioresample", g_strdup_printf("audio-resample%d", tdx));
            main[i].videoPipe[idx].audioPipe.encoder = gst_element_factory_make("lamemp3enc", g_strdup_printf("audio-enc%d", tdx));
            main[i].videoPipe[idx].audioPipe.parse = gst_element_factory_make("mpegaudioparse", g_strdup_printf("audio-parse%d", tdx));
            main[i].videoPipe[idx].audioPipe.queue = gst_element_factory_make("queue", g_strdup_printf("audio-queue%d", tdx));
            main[i].videoPipe[idx].audioPipe.tee = gst_element_factory_make("tee", g_strdup_printf("audio-tee%d", tdx));
            if (!main[i].videoPipe[idx].audioPipe.src || !main[i].videoPipe[idx].audioPipe.convert || !main[i].videoPipe[idx].audioPipe.resample \
                    || !main[i].videoPipe[idx].audioPipe.encoder || !main[i].videoPipe[idx].audioPipe.parse || !main[i].videoPipe[idx].audioPipe.queue || !main[i].videoPipe[idx].audioPipe.tee)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            g_object_set(main[i].videoPipe[idx].audioPipe.src, "is-live", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.src, "wave", 8, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.src, "tick-interval", 200000000, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.src, "apply-tick-ramp", TRUE, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-buffers", 60, NULL);
            //g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-bytes", 0, "max-size-time", 5*GST_SECOND, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].audioPipe.queue, "max-size-buffers", 60, "leaky", 1, NULL);

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.resample, \
                        main[i].videoPipe[idx].audioPipe.encoder, main[i].videoPipe[idx].audioPipe.parse, main[i].videoPipe[idx].audioPipe.queue, main[i].videoPipe[idx].audioPipe.tee, NULL);
            if (!gst_element_link_many(main[i].videoPipe[idx].audioPipe.src, main[i].videoPipe[idx].audioPipe.convert, main[i].videoPipe[idx].audioPipe.encoder, \
                        main[i].videoPipe[idx].audioPipe.queue, main[i].videoPipe[idx].audioPipe.parse, main[i].videoPipe[idx].audioPipe.tee, NULL))
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] audio pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            //if(gst_pad_link(gst_element_get_static_pad(main[i].videoPipe[idx].audioPipe.tee, "src"), \
                            gst_element_get_request_pad(main[i].videoPipe[idx].sink, "audio_%u")) != GST_PAD_LINK_OK)
            audio_sink_pad[customData[tdx].ch] = gst_element_get_request_pad(main[i].videoPipe[idx].sink, "audio_%u");
            if(gst_pad_link(gst_element_get_request_pad(main[i].videoPipe[idx].audioPipe.tee, "src_%u"), \
                            audio_sink_pad[customData[tdx].ch]) != GST_PAD_LINK_OK)
            {
                g_message("audio pad link err");
                return -1;
            }

#endif
            //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)sendEOS, &customData[tdx]);
            //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, &customData[tdx]);
        } while(0);
#endif

#ifdef RTSP_L_ENABLE
        do
        {
            idx = RTSP0_L;
            tdx = idx+i*MAX_ENC;
            main[i].videoPipe[idx].videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", tdx));
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));
            main[i].videoPipe[idx].timeoverlay = gst_element_factory_make("timeoverlay", g_strdup_printf("timeoverlay%d", tdx));

            if (!main[i].videoPipe[idx].videorate || !main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].timeoverlay || !main[i].videoPipe[idx].encoder \
                || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].timeoverlay, \
                            main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].timeoverlay, \
                            main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            
            g_object_set(main[i].videoPipe[idx].timeoverlay, "valignment", 2, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "halignment", 0, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Times New Roman Italic, 16", NULL);
            //g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Arial Bold, 16", NULL);
            //g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Helvetica Bold Italic, 16", NULL);
            //g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "DejaVu Serif, 16", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "show-times-as-dates", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-epoch", g_date_time_new_now_local(), NULL);

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "max-rate", RTSP_FPS, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "drop-only", FALSE, NULL);
            g_object_set(main[i].videoPipe[idx].encoder, "bitrate", RTSP_BITRATE, NULL);
            //g_object_set(encoder2, "gop-size", 30, NULL);
            //g_object_set(appsink2, "emit-signals", TRUE, "sync", FALSE, NULL);
            //g_object_set(appsink2, "max-lateness", 5000000000, NULL);
            //g_object_set(queue2, "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-time", 5*GST_SECOND, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
#if 0
            g_object_set(queue2, "max-size-bytes", 0, NULL);
            g_object_set(queue2, "max-size-time", 0, NULL);
            g_object_set(queue2, "max-size-buffers", 60, NULL);
            g_object_set(queue2, "leaky", 1, NULL);
            //g_object_set(appsink2, "max-buffers", 1, NULL);
            //g_object_set(queue2, "flush-on-eos", TRUE, NULL);
            //g_signal_connect(queue2, "underrun", G_CALLBACK(underrun_callback), NULL);
            //g_signal_connect(queue2, "overrun", G_CALLBACK(overrun_callback), NULL);
            //g_signal_connect(queue2, "running", G_CALLBACK(running_callback), NULL);
            //g_signal_connect(queue2, "pushing", G_CALLBACK(pushing_callback), NULL);
#endif

            g_object_set(main[i].videoPipe[idx].sink, "max-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "drop", TRUE, NULL);
            //g_object_set(appsink2, "enable-last-sample", TRUE, NULL);
            //g_object_set(appsink2, "max-lateness", 1000, NULL);
            //g_object_set(appsink2, "throttle-time", 100, NULL);
            //g_object_set(appsink2, "processing-deadline", 200, NULL);
            //g_object_set(appsink2, "render-delay", 1000, NULL);
            //g_object_set(appsink2, "wait-on-eos", FALSE, NULL);
            //g_object_set(appsink2, "qos", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", TRUE, NULL);
            //g_object_set(appsink2, "max-bitrate", 2048, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "eos", G_CALLBACK(eos_callback), NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            //g_signal_connect(appsink2, "pad-added", G_CALLBACK(pad_added_handler), capsfilter);
            //g_signal_connect(appsink2, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);
            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].timeoveraly = main[i].videoPipe[idx].timeoverlay;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = STREAMING;
            customData[tdx].is_live = TRUE;
            setRtspPipe(&customData[tdx]);
            //g_timeout_add_seconds(1, (GSourceFunc)setSRT, &customData[tdx]);
        } while(0);
#endif

#ifdef RTSP_R_ENABLE
        do
        {
            //if(i==1)break;
            idx = RTSP0_R;
            tdx = idx+i*MAX_ENC;
            main[i].videoPipe[idx].videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", tdx));
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));
            main[i].videoPipe[idx].timeoverlay = gst_element_factory_make("timeoverlay", g_strdup_printf("timeoverlay%d", tdx));

            if (!main[i].videoPipe[idx].videorate || !main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].timeoverlay || !main[i].videoPipe[idx].encoder \
                || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].timeoverlay, \
                            main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].timeoverlay, \
                            main[i].videoPipe[idx].videorate, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }
            
            g_object_set(main[i].videoPipe[idx].timeoverlay, "valignment", 2, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "halignment", 0, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "font-desc", "Times New Roman Italic, 16", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "show-times-as-dates", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].timeoverlay, "datetime-epoch", g_date_time_new_now_local(), NULL);

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].videorate, "max-rate", RTSP_FPS, NULL);
            g_object_set(main[i].videoPipe[idx].videorate, "drop-only", FALSE, NULL);
            g_object_set(main[i].videoPipe[idx].encoder, "bitrate", RTSP_BITRATE, NULL);
            //g_object_set(encoder2, "gop-size", 30, NULL);
            //g_object_set(appsink2, "emit-signals", TRUE, "sync", FALSE, NULL);
            //g_object_set(appsink2, "max-lateness", 5000000000, NULL);
            //g_object_set(queue2, "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", 1, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-time", 5*GST_SECOND, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);

            g_object_set(main[i].videoPipe[idx].sink, "max-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "drop", TRUE, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", TRUE, NULL);
            //g_object_set(appsink2, "max-bitrate", 2048, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "eos", G_CALLBACK(eos_callback), NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            //g_signal_connect(appsink2, "pad-added", G_CALLBACK(pad_added_handler), capsfilter);
            //g_signal_connect(appsink2, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);
            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].timeoveraly = main[i].videoPipe[idx].timeoverlay;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = STREAMING;
            customData[tdx].is_live = TRUE;
            setRtspPipe(&customData[tdx]);
            //g_timeout_add_seconds(1, (GSourceFunc)setSRT, &customData[tdx]);
        } while(0);
#endif

#ifdef CAPTURE_L_ENABLE
        do
        {
            idx = CAPTURE0_L;
            tdx = idx+i*MAX_ENC;
            //main[i].videoPipe[CAPTURE_L].videorate = gst_element_factory_make("videorate", "videorate4");
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));

            if (!main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].encoder || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", FALSE, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = CAPTURING;
            customData[tdx].captureMax = 3;
            customData[tdx].captureCnt = 3;
            //g_timeout_add_seconds(30, (GSourceFunc)captureStart, &customData[tdx]);
        } while(0);
#endif

#ifdef CAPTURE_R_ENABLE
        do
        {
            idx = CAPTURE0_R;
            tdx = idx+i*MAX_ENC;
            //main[i].videoPipe[CAPTURE_L].videorate = gst_element_factory_make("videorate", "videorate4");
            main[i].videoPipe[idx].crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", tdx));
            main[i].videoPipe[idx].encoder = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", tdx));
            main[i].videoPipe[idx].queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", tdx));
            main[i].videoPipe[idx].sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", tdx));

            if (!main[i].videoPipe[idx].crop || !main[i].videoPipe[idx].encoder || !main[i].videoPipe[idx].queue || !main[i].videoPipe[idx].sink)
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] create error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            gst_bin_add_many(GST_BIN(pipeline[i]), main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL);
            if (!gst_element_link_many(main[i].tee, main[i].videoPipe[idx].crop, main[i].videoPipe[idx].queue, main[i].videoPipe[idx].encoder, main[i].videoPipe[idx].sink, NULL)) 
            {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, idx);
                //gst_object_unref(pipeline[i]);
                break;
            }

            if(idx%2 == 0)
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
            else
                g_object_set(main[i].videoPipe[idx].crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

            g_object_set(main[i].videoPipe[idx].queue, "max-size-buffers", 60, NULL);
            g_object_set(main[i].videoPipe[idx].queue, "leaky", 2, NULL);
            g_object_set(main[i].videoPipe[idx].sink, "emit-signals", TRUE, "sync", FALSE, NULL);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-sample", G_CALLBACK(new_sample_handler), &customData[tdx]);
            g_signal_connect(main[i].videoPipe[idx].sink, "new-preroll", G_CALLBACK(new_preroll_handler), &customData[tdx]);
            customData[tdx].index = tdx;
            customData[tdx].enc = main[i].videoPipe[idx].encoder;
            customData[tdx].vr = main[i].videoPipe[idx].videorate;
            customData[tdx].ch = (idx)%2 + i*2;
            customData[tdx].mode = CAPTURING;
            customData[tdx].captureMax = 3;
            customData[tdx].captureCnt = 3;
            //g_timeout_add_seconds(30, (GSourceFunc)captureStart, &customData[tdx]);
        } while(0);
#endif

        //pipeline2 = GST_ELEMENT(rtspServer);

        // 캡처 포맷 설정 (video3 장치의 적절한 캡처 포맷에 맞게 변경)
#if 1
        g_object_set(main[i].src, "do-timestamp", TRUE, NULL);
        g_object_set(main[i].src, "device", g_strdup_printf("/dev/video%d", i+3), NULL);
        __LOG(LOG_NOTICE, "[GST][%s:%d] src[%d] : /dev/video%d", _FILE_, __LINE__, i, i+3);
        //g_object_set(main[i].src, "device", "/dev/video3", NULL);

        caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "NV12",
                                            "width", G_TYPE_INT, _WIDTH*2,
                                            "height", G_TYPE_INT, _HEIGHT,
                                            "framerate", GST_TYPE_FRACTION, MAIN_FPS, 1,
                                            NULL);


        g_object_set(main[i].capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);
#endif
        //change_file_datetime(NULL);
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)change_file_datetime, NULL);
        //main[i].pipeline = pipeline[0];
#if defined(MP4_TEST_L) || defined(MP_TEST_R)
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)sendEOS, &main[i]);
#endif

#if 0
        // 파이프라인 실행
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipe(%d) play", _FILE_, __LINE__, i);
        ret = gst_element_set_state(pipeline[i], GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipeline[%d] playing error", _FILE_, __LINE__, i);
            gst_object_unref(pipeline[i]);
            return -1;
        } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
            customData->is_live = TRUE;
        }

        main[i].bus = gst_element_get_bus(pipeline[i]);
        if(!main[i].bus) {
            __LOG(LOG_CRIT, "[GST][%s:%d] bus[%d] get error from pipeline", _FILE_, __LINE__, i);
        }
        //main[i].pipeline = pipeline[i];
        main[i].index = i;
        main[i].bus_watch_id = gst_bus_add_watch(main[i].bus, my_bus_callback, &main[i]);

        gst_object_unref(main[i].bus);
#endif
    } while(++i < MAX_PIPELINE);

    i = 0;
    do {
        // 파이프라인 실행
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipe(%d) play", _FILE_, __LINE__, i);
        ret = gst_element_set_state(pipeline[i], GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipeline[%d] playing error", _FILE_, __LINE__, i);
            gst_object_unref(pipeline[i]);
            return -1;
        } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
            //customData->is_live = TRUE;
        }
#if 1
        main[i].bus = gst_element_get_bus(pipeline[i]);
        if(!main[i].bus) {
            __LOG(LOG_CRIT, "[GST][%s:%d] bus[%d] get error from pipeline", _FILE_, __LINE__, i);
        }
        main[i].pipeline = pipeline[i];
        main[i].index = i;
        main[i].bus_watch_id = gst_bus_add_watch(main[i].bus, my_bus_callback, &main[i]);

        gst_object_unref(main[i].bus);
#endif

    } while(++i < MAX_PIPELINE);
#if 0
    //GstClock *clock = gst_element_get_clock(pipeline);
    //gst_clock_set_calibration(clock, 1.0);
    //GstClock *system_clock = gst_system_clock_obtain();
    GstClockTime frame_duration = gst_util_uint64_scale_int(1, GST_SECOND, 30);
    g_print("duration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(frame_duration));
    GstClock *pipeline_clock = gst_pipeline_get_clock(GST_PIPELINE(pipeline));
    //gst_pipeline_use_clock(GST_PIPELINE(pipeline), system_clock);
    gst_clock_set_calibration(pipeline_clock, GST_CLOCK_TIME_NONE, frame_duration, 1, 1);
#endif

    sleep(3);
    guint srtTimer_id = g_timeout_add_seconds(1, (GSourceFunc)setSRT, customData);
    //guint terminalInput_id = g_timeout_add_seconds(1, (GSourceFunc)check_terminal_input, customData);
#ifdef TERMINAL_CMD_ENABLE
    terminalThread = g_thread_new("terminal-thread", (GThreadFunc)check_terminal_input, customData);
#endif
    //rtspThreadFunc(&info[0]);
    //rtspThreadFunc(&info[1]);
    //pthread_create(&m_threadRtsp2, NULL, &rtspThreadFunc, &info[0]);
    //pthread_create(&m_threadRtsp3, NULL, &rtspThreadFunc, &info[1]);
    //g_timeout_add_seconds(15, (GSourceFunc)changeBitrate, &main[i].videoPipe[FILE_L].customData);
    //g_timeout_add_seconds(30, (GSourceFunc)changeFPS, &main[i].videoPipe[FILE_L].customData);

    //g_timeout_add_seconds(10, (GSourceFunc)captureStart, &customData[CAPTURE_L]);


    ipcThread = g_thread_new("ipc-thread", (GThreadFunc)ipcLoop, customData);
    gstLoop = g_main_loop_new(NULL, FALSE);

	if(!gstLoop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Main loop create error", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop start", _FILE_, __LINE__);
        //g_main_loop_run(gstLoop);
        while (!is_interrupted)
        {
            g_main_context_iteration(g_main_loop_get_context(gstLoop), FALSE);
            taskLoop(customData);
        }
    }
    __LOG(LOG_CRIT, "[GST][%s:%d] Main loop end", _FILE_, __LINE__);
    
    //gst_element_get_state(pipeline, NULL, NULL, 2*GST_SECOND);

#if 1
    //i = 1;
    //sleep(1);

    for(i=0; i<MAX_PIPELINE; i++)
    {
#ifdef FILE_L_ENABLE
        //customData[FILE0_L+i*MAX_ENC].is_live = FALSE;
        //splitNow(&customData[FILE0_L+i*MAX_ENC]);
#endif

#ifdef FILE_R_ENABLE
        //customData[FILE0_R+i*MAX_ENC].is_live = FALSE;
        //splitNow(&customData[FILE0_R+i*MAX_ENC]);
#endif
        //sleep(1);
        //gst_element_send_event(pipeline[i], gst_event_new_eos());
        if(pipeline[i])
        {
            //gst_element_get_state(pipeline[i], &state, NULL, GST_CLOCK_TIME_NONE);
            __LOG(LOG_NOTICE, "[GST][%s:%d] pipe[%d] off", _FILE_, __LINE__, i);
            //gst_element_send_event(pipeline[i], gst_event_new_eos());
            //GstMessage *eos_msg = gst_message_new_eos(GST_OBJECT(pipeline[i]));
            //gst_element_post_message(pipeline[i], eos_msg);

            // Wait for the pipeline to finish
            //GstBus *bus = gst_element_get_bus(pipeline[i]);
            //gst_bus_poll(bus, GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);
            //gst_object_unref(bus);

            gst_element_set_state(pipeline[i], GST_STATE_NULL);
            gst_object_unref(pipeline[i]);
        }
    }
    
#endif
    ipc_clear();
#ifdef TERMINAL_CMD_ENABLE
    g_thread_join(terminalThread);
#endif

#if defined(RTSP_L_ENABLE) || defined(RTSP_R_ENABLE)
    g_object_unref(rtspServer);
    g_source_remove(cleanRtsp_id);
#endif
    g_source_remove(srtTimer_id);
    //g_source_remove(terminalInput_id);
    g_main_loop_unref(gstLoop);
    
    __LOG(LOG_CRIT, "[GST][%s:%d] exit", _FILE_, __LINE__);
    exit(EXIT_SUCCESS);
    

    return 0;
}

#if 0
static void	media_constructed(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
    PipeMain *info = (PipeMain*)user_data;
	GstElement *element;
    //GstElement *queue;
    gchar *queue_name;
    //info->queue
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    queue_name = g_strdup_printf("%s%d", QUEUE_NAME, info->ch);
    GstElement *queue = gst_bin_get_by_name(GST_BIN(info->pipeline), queue_name);

    g_print("queue_name : %s\n", queue_name);

    if (queue) {
        //gst_element_send_event(queue, gst_event_new_flush_start());
        gst_element_send_event(queue, gst_event_new_flush_stop(TRUE));
        g_print("Queue flushed\n");
    }

    queue_name = g_strdup_printf("%s%d", QUEUE_RTSP_NAME, info->ch);
    queue = gst_bin_get_by_name(GST_BIN(pipeline), queue_name);
    

    //gst_object_unref(queue);
    g_free(queue_name);

    return;
}

static gboolean handle_client_disconnected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) {
    // This function is called when a client is disconnected from the RTSP server.
    // You can clean up resources associated with the client session here.
    const gchar *ip_address = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, ip_address);
    // Clean up session for the disconnected client
    
    return TRUE;
}


static GstFlowReturn new_sample_handler_capture(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)data;
    //GstClockTime timestamp;

    // sample을 가져오기
    //g_signal_emit_by_name(appsink, "pull-sample", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }

#if 1
    if(info->captureCnt >= info->captureMax)
    {
        //__LOG(LOG_DEBUG, "[GST][%s:%d] captureMax", _FILE_, __LINE__);
        //gst_buffer_unmap(buffer, &map);
        //gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
#endif
    __LOG(LOG_DEBUG, "[GST][%s:%d] captureCnt %d captureMax %d", _FILE_, __LINE__, info->captureCnt, info->captureMax);

    // 버퍼 가져오기
    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 파일로 저장
    GstMapInfo map;
    gchar *path;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    //g_print("file_name:%s\n", info->file_name);

    path = g_strdup_printf("%s%s_%d.jpg", FILE_PATH, info->file_name, info->captureCnt++);

    //g_print("path : %s\n", path);
    FILE *file = fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, info->file_name);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    g_free(path);

    return GST_FLOW_OK;
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
    gchar *path;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    //g_print("file_name:%s\n", info->file_name);

    path = g_strdup_printf("%s%s", FILE_PATH, info->file_name);

    //g_print("path : %s\n", path);
    FILE *file = fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, info->file_name);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    g_free(path);

    return GST_FLOW_OK;
}

gboolean gst_rtsp_stream_server_push_data(gpointer userData, gpointer data, gint size, gboolean key_frame_flag, GstBuffer *buffer)
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


	buf = gst_buffer_new_and_alloc(size);

	bret = gst_buffer_map(buf, &map, GST_MAP_WRITE);
	if(!bret) 
	{
		g_printerr("map fail!\n");
		goto error;
	}

	//memcpy(map.data, data, size);
	//map.size = size;

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



done:
	//g_mutex_unlock(&stream->lock);
	return TRUE;

error:
	//g_mutex_unlock(&stream->lock);

	return FALSE;
}

static GStaticMutex gmutex = G_STATIC_MUTEX_INIT; // Static mutex for synchronization

static void process_buffer(gpointer userData) {
    CustomData *info = (CustomData *)userData;
    // Perform data processing on the buffer
    //g_print("process_buffer\n");
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
}

static void process_data_thread(gpointer userData) {
    CustomData *info = (CustomData *)userData;
    // Acquire the mutex before processing the buffer
    //g_static_mutex_lock(&gmutex);
    //g_print("process_data_thread\n");
    process_buffer(info);

    // Release the mutex after processing
    //g_static_mutex_unlock(&gmutex);
}


static GstFlowReturn new_sample_handler_rtsp(GstElement *sink, gpointer userData) {
    GstSample *sample = NULL;
    GstBuffer *buffer = NULL;
    CustomData *info = (CustomData *)userData;
    //static gboolean start_f = FALSE;
    //static gboolean push_1st_iframe = FALSE;
    gboolean key_frame_flag;
    //static GstClockTime recording_start_time = GST_CLOCK_TIME_NONE;
    //GstBuffer* buf = NULL;
    GstMapInfo map;
    //static guint16 i = 0;
    //static GstClockTime dts = GST_CLOCK_TIME_NONE;
    //GstMapInfo map1;

	if( (info == NULL) || (sink == NULL) )
		return GST_FLOW_ERROR;
#if 0
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
        g_print("Current caps: %s\n", caps_str);
        //info->caps = caps; 

        g_object_set(info->appsrc, "caps", caps, NULL);
        g_free(caps_str);
        gst_caps_unref(caps);
        gst_object_unref(pad);
        start_f = TRUE;
    }
#endif

    //g_signal_emit_by_name(sink, "pull-sample", &sample);
    //g_signal_emit_by_name(sink, "pull-preroll", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        __LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        goto error;
    }
    gst_sample_unref(sample);

    buffer = gst_sample_get_buffer (sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);

        goto error;
    }

#if 0
	if(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) 
		key_frame_flag = FALSE;
	else 
		key_frame_flag = TRUE; 
#endif

#if 0

 #if 1
	if(GST_STATE(GST_ELEMENT(info->appsrc)) < GST_STATE_PAUSED)
	{
        //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));
        //g_print("appsrc state NULL or READY\n");
        //GST_BUFFER_TIMESTAMP(buffer) = GST_CLOCK_TIME_NONE;
        //GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
        //gst_buffer_set_flags(buffer, GST_BUFFER_FLAG_RESYNC);
        //gst_buffer_set_flags(buffer, GST_BUFFER_FLAG_DROPPABLE);
        //gst_buffer_set_flags(buffer, GST_BUFFER_FLAG_LIVE);
        //gst_buffer_unmap(buffer, &map);
        //i = 0;
        //gst_clock_set_time(clock, 0);
        //dts = 0;
		goto error;
	}
#endif

    //if (gst_buffer_map(buffer, &map, GST_MAP_READ)) 
    {
        // Push the data to appsrc
        //GST_BUFFER_TIMESTAMP(buffer) = 0;
        //static guint16 i = 0;
        GST_BUFFER_DTS(buffer) = dts;
        //gst_buffer_set_flags(buffer, GST_BUFFER_FLAG_DROPPABLE);
        //gst_buffer_set_flags(buffer, GST_BUFFER_FLAG_LIVE);
        //g_print("DTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(dts)); 
        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));
        //dts += gst_util_uint64_scale_int(1, GST_SECOND, 15);
        if (ret != GST_FLOW_OK) {
            //g_print("Failed to push buffer to appsrc\n");
        }

        // Unmap the buffer
        //gst_buffer_unmap(buffer, &map);
    }

#elif 0
    
	//if(sample) 
	{
		GstBuffer *buffer = gst_sample_get_buffer (sample);


		if(!gst_buffer_map(buffer, &map, GST_MAP_READ)) 
		{
			g_printerr("gst_buffer_map fail !!!\n");
			return GST_FLOW_ERROR;
		}

		gpointer data = map.data;
		gint size = map.size;

        GstMapInfo *buf = gst_buffer_new_and_alloc(size);
        //GstBuffer *buf = gst_buffer_new_and_alloc(map.size);

        if(!gst_buffer_map(buf, &map, GST_MAP_WRITE))
        {
            g_printerr("map fail!\n");
            return GST_FLOW_ERROR;
        }
        //memcpy(map.data, data, size);
        //map.size = size;

        //gst_buffer_unmap(buf, &map);

        gst_buffer_copy_into(buf, buffer, GST_BUFFER_COPY_MEMORY, 0, -1);     
        gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), buf);
        //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buf));
        //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));        
		//gst_buffer_unmap(buffer, &map);

		//gst_rtsp_stream_server_push_data(info, data, size, key_frame_flag, buffer);
        gst_buffer_unmap(buffer, &map);
		gst_sample_unref (sample);
		return GST_FLOW_OK;
	}
#elif 0
		if(!gst_buffer_map(buffer, &map, GST_MAP_READ)) 
		{
			g_printerr("gst_buffer_map fail !!!\n");
			return GST_FLOW_ERROR;
		}

		gpointer data = map.data;
		gint size = map.size;
		gst_rtsp_stream_server_push_data(info, data, size, key_frame_flag, buffer);
        gst_buffer_unmap(buffer, &map);
		//gst_sample_unref (sample);
		return GST_FLOW_OK;

#else

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // Create a new buffer and copy data
        info->buf = gst_buffer_new_and_alloc(map.size);
        gst_buffer_fill(info->buf, 0, map.data, map.size);

        // Unmap the original buffer
        gst_buffer_unmap(buffer, &map);
        //info->buf = newBuffer;
        
        // Push the new buffer to the appsrc
        //g_thread_new("data-processing-thread", (GThreadFunc)process_data_thread, info);
        gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
    } else {
        g_printerr("Failed to map buffer\n");
    }

#endif

#if 0
	if(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    {
		key_frame_flag = FALSE;
        g_print("key_frame_flag FALSE\n");
    }
	else 
		key_frame_flag = TRUE;
#endif


#if 0
	if(!push_1st_iframe) 
	{
		if(!key_frame_flag) 
		{
			 return GST_FLOW_ERROR;
		}
	}
#endif

    //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), gst_buffer_copy(buffer));
    //gst_buffer_copy_into(buf, buffer, GST_BUFFER_COPY_MEMORY, 0, -1);
    //GST_BUFFER_TIMESTAMP(buffer) = 0; 
    //gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), buffer); 
    

    //gst_buffer_unmap(buffer, &map);

    //gst_buffer_unmap(buf, &map);
    //gst_buffer_unref (buffer);
    //g_signal_emit_by_name(info->appsrc, "push-buffer", &buffer);
#if 0
	if(!push_1st_iframe) 
	{
		if(key_frame_flag) 
		{
			push_1st_iframe = TRUE;
			__LOG(LOG_NOTICE, "[GST][%s:%d] push_1st_iframe", _FILE_, __LINE__);
		}
	}
#endif

done:
	//g_mutex_unlock(&stream->lock);
    //g_printerr("1\n");
    //if(!sample) gst_sample_unref(sample);
    //g_printerr("2\n");
    //if(!buffer) gst_buffer_unref (buffer);
    
    //g_printerr("3\n");

	return GST_FLOW_OK;

error:
	//g_mutex_unlock(&stream->lock);
    //g_printerr("4\n");
    //if(!sample) gst_sample_unref(sample);
    //g_printerr("5\n");
    //if(!buffer) gst_buffer_unref (buffer);
    //if(!buf) gst_buffer_unmap(buf, &map);
    //g_printerr("6\n");
    
	return GST_FLOW_ERROR;
}



#endif