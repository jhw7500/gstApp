
#include <stdio.h>
#include <gst/gst.h>
#include <signal.h>
//#include <glib.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>

#define GST_DEUBG=3

typedef struct _CustomData
{
    GstElement *pipeline;
    GstElement *bin_left;
    GstElement *bin_right;
    GstElement *source;
    GstElement *filesink_left;
    GstElement *filesink_right;
    GstElement *rtspsink_left;
    GstElement *rtspsink_right;
    GstElement *tee_crop;
    GstElement *queue_left;
    GstElement *queue_right;
    GstElement *queue_file_left;
    GstElement *queue_file_right;
    GstElement *queue_rtsp_left;
    GstElement *queue_rtsp_right;
    GstElement *tee_left;
    GstElement *tee_right;
    GstElement *encoder_file_left;
    GstElement *encoder_file_right;
    GstElement *encoder_rtsp_left;
    GstElement *encoder_rtsp_right;
    GstElement *crop_left;
    GstElement *crop_right;
    GstElement *capsfilter;
    GstElement *parse_file_left;
    GstElement *parse_file_right;
    GstElement* parse_rtsp_left;
    GstElement* parse_rtsp_right;
    GstElement *convert;
    GstElement *rtspsrc_left;
    GstCaps *caps;
} CustomData;

typedef struct {
  gboolean white;
  GstClockTime timestamp;
} MyContext;

static CustomData data;
static GgstLoop *loop;
pthread_t m_threadRtsp;

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
            g_main_loop_quit(loop);
            break;
        }

        case GST_MESSAGE_EOS:
        {
            g_main_loop_quit(loop);
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
            GstTagList *tags = NULL;

            gst_message_parse_tag (message, &tags);
            g_print ("Got tags from element %s\n", GST_OBJECT_NAME (message->src));
            gst_tag_list_foreach (tags, print_tag, NULL);
            gst_tag_list_free (tags);
            //handle_tags (tags);
            gst_tag_list_unref (tags);
            break;
        }

        default:
            break;
    }

    return TRUE;
}

static void cb_new_pad(GstElement *element, GstPad *pad, gpointer data)
{
    gchar *name;

    name = gst_pad_get_name(pad);
    g_print("A new pad %s was created\n", name);

    g_free(name);
}

#if 0
static void appendPipelineNeedContextMessageCallback(GstBus*, GstMessage* message, AppendPipeline* appendPipeline)
{
    GST_TRACE("received callback");
    appendPipeline->handleNeedContextSyncMessage(message);
}

void show_message_dialog(const gchar * const message)
{

    GtkWidget*  dialog = gtk_message_dialog_new (GTK_WINDOW(mainwindow),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_CLOSE,
                                     "Message: %s",
                                     message);

    /* Destroy the dialog when the user responds to it (e.g. clicks a button) */
    g_signal_connect_swapped (dialog, "response",
                              G_CALLBACK (gtk_widget_destroy),
                              dialog);
    gtk_widget_show_all (dialog);
    return;
}
#endif

static void
pad_added_handler (GstElement * src, GstPad * new_pad, CustomData * data)
{
  GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad),
      GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
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

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}


void cb_message_error(GstMessage* message)
{
    printf("state change\n");
    return;
}

static void some_function(GstElement *tee)
{
    GstPad *pad;
    gchar *name;

    //pad = gst_element_get_request_pad(tee, "src%d");
    pad = gst_element_request_pad_simple(tee, "src_%u");
    name = gst_pad_get_name(pad);
    g_print("A new pad %s was created\n", name);
    g_free(name);

    gst_object_unref(GST_OBJECT(pad));
}

static void link_to_multiplexer(GstPad *tolink_pad, GstElement *mux)
{
    GstPad *pad;
    gchar *srcname, *sinkname;

    srcname = gst_pad_get_name(tolink_pad);
    pad = gst_element_get_compatible_pad(mux, tolink_pad, NULL);
    gst_pad_link(tolink_pad, pad);
    sinkname = gst_pad_get_name(pad);
    gst_object_unref(GST_OBJECT(pad));
    g_print("A new pad %s was created and linked to %s\n", srcname, sinkname);

    g_free(sinkname);
    g_free(srcname);
}

static void read_video_props(GstCaps *caps)
{
    gint width,height;
    const GstStructure *str;

    g_return_if_fail(gst_caps_is_fixed(caps));

    str = gst_caps_get_structure(caps, 0);

    if(!gst_structure_get_int(str, "width", &width) || !gst_structure_get_int(str, "height", &height)) {
        g_print("No width/height availablen");
        return;
    }

    g_print("The video size of this set of capabilities is %dx%d\n", width, height);

}

static gboolean link_elements_with_filter(GstElement *element1, GstElement *element2)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple("video/x-raw",
                                "format", G_TYPE_STRING, "1420",
                                "width", G_TYPE_INT, 384,
                                "height", G_TYPE_INT, 288,
                                "framerate", GST_TYPE_FRACTION, 25, 1,
                                NULL);

    link_ok = gst_element_link_filtered(element1, element2, caps);
    gst_caps_unref(caps);

    if(!link_ok) {
        g_warning("Failed to link element1 and element2!\n");
    }

    return link_ok;
}

static gboolean link_elements_with_filter_full(GstElement *element1, GstElement *element2)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_full(gst_structure_new("video/x-raw",
                                //"format", G_TYPE_STRING, "1420",
                                "width", G_TYPE_INT, 384,
                                "height", G_TYPE_INT, 288,
                                "framerate", GST_TYPE_FRACTION, 25, 1,
                                NULL),
                            gst_structure_new("video/x-bayer",
                                //"format", G_TYPE_STRING, "1420",
                                "width", G_TYPE_INT, 384,
                                "height", G_TYPE_INT, 288,
                                "framerate", GST_TYPE_FRACTION, 25, 1,
                                NULL),
                            NULL);

    link_ok = gst_element_link_filtered(element1, element2, caps);
    gst_caps_unref(caps);

    if(!link_ok) {
        g_warning("Failed to link element1 and element2!\n");
    }

    return link_ok;
}

void sigintHandler(int unused) {
	g_print("You ctrl-c-ed!");
	gst_element_send_event(data.pipeline, gst_event_new_eos()); 
	return;
}

void sigkillHandler(int unused) {
	g_print("You killed!");
	gst_element_send_event(data.pipeline, gst_event_new_eos()); 
	return;
}

void sigtermHandler(int unused) {
	g_print("You terminated!");
	gst_element_send_event(data.pipeline, gst_event_new_eos()); 
	return;
}

/* called when we need to give data to appsrc */
static void need_data (GstElement * appsrc, guint unused, MyContext * ctx)
{
  GstBuffer *buffer;
  guint size;
  GstFlowReturn ret;

  size = 385 * 288 * 2;

  buffer = gst_buffer_new_allocate (NULL, size, NULL);

  /* this makes the image black/white */
  gst_buffer_memset (buffer, 0, ctx->white ? 0xff : 0x0, size);

  ctx->white = !ctx->white;

  /* increment the timestamp every 1/2 second */
  GST_BUFFER_PTS (buffer) = ctx->timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);
  ctx->timestamp += GST_BUFFER_DURATION (buffer);

  g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
}

// appsink로부터 데이터를 받는 콜백 함수 정의
static GstFlowReturn on_new_sample(GstElement *appsink, gpointer user_data) {
    GstSample *sample = NULL;
    GstBuffer *buffer = NULL;
    GstElement *appsrc = (GstElement *)user_data;

    // appsink에서 샘플 받기
    sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (sample) {
        buffer = gst_sample_get_buffer(sample);
        if (buffer) {
            // appsrc로 데이터 전송
            gst_app_src_push_buffer(GST_APP_SRC(appsrc), gst_buffer_copy(buffer));
        }
        gst_sample_unref(sample);
    }

    return GST_FLOW_OK;
}

#if 0
// 60초마다 파일 이름 변경하는 타이머 콜백 함수 정의
static gboolean change_file_name(GstElement *filesink) {
    gchar *filename;
    GstState state;

    gst_element_get_state(filesink, &state, NULL, GST_CLOCK_TIME_NONE);

    // 파일 이름 변경은 PAUSED 상태에서만 가능
    if (state == GST_STATE_PAUSED) {
        // 현재 날짜와 시간으로 파일 이름 생성
        GDateTime *datetime = g_date_time_new_now_local();
        gchar *date_str = g_date_time_format(datetime, "%Y%m%d%H%M%S");
        filename = g_strdup_printf("output_%s.mp4", date_str);
        g_date_time_unref(datetime);
        g_free(date_str);

        // 파일 이름 변경
        g_object_set(filesink, "location", filename, NULL);
        g_free(filename);
    }

    // 타이머 재등록
    return TRUE;
}
#endif
#if 1
#define CUSTOM_TYPE_MEDIA_FACTORY (custom_media_factory_get_type())
G_DECLARE_FINAL_TYPE(CustomMediaFactory, custom_media_factory, CUSTOM, MEDIA_FACTORY, GstElement)
//#define CUSTOM_TYPE_MEDIA_FACTORY GST_TYPE_RTSP_MEDIA_FACTORY
//#define CUSTOM_MEDIA_FACTORY(obj)   (G_TYPE_CHECK_INSTANCE_CAST ((obj), CUSTOM_TYPE_MEDIA_FACTORY, CustomMediaFactory))
typedef struct _CustomMediaFactory CustomMediaFactory;
typedef struct _CustomMediaFactory {
    GstRTSPMediaFactory parent;
    GstElement *my_bin; // 사용자 정의 bin을 저장하기 위한 변수
} CustomMediaFactory;

G_DEFINE_TYPE(CustomMediaFactory, custom_media_factory, GST_TYPE_RTSP_MEDIA_FACTORY)

void
gst_rtsp_media_factory_custom_set_bin (CustomMediaFactory * factory,
    GstElement * bin)
{
  //g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY_CUSTOM (factory));
  //g_return_if_fail (bin != NULL);

  //g_mutex_lock (CUSTOM_MEDIA_FACTORY(factory)->lock);

  if (factory->my_bin)
      gst_object_unref (factory->my_bin);
  if (bin)
      gst_object_ref (bin);
  factory->my_bin = bin;

  //g_mutex_unlock (CUSTOM_MEDIA_FACTORY(factory)->lock);
}

// set_property 메서드 재정의
static void custom_media_factory_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    CustomMediaFactory *factory = CUSTOM_MEDIA_FACTORY(object);
    g_print("custom_media_factory_set_property\n");
    switch (prop_id) {
        case 1: {
            // "my-bin" 프로퍼티를 설정하는 경우 사용자 정의 bin을 가져옴
#if 0
            GstElement *bin = GST_ELEMENT(g_value_get_object(value));
            if (GST_IS_BIN(bin)) {
                factory->my_bin = bin;
                g_print("set bin!\n");
            }
            else
                g_print("not set bin!\n");
#endif
            //factory->my_bin = GST_ELEMENT(g_value_get_object(value));
            //factory->my_bin = GST_ELEMENT(g_value_dup_object(value));
            g_clear_object(&factory->my_bin);
            factory->my_bin = GST_ELEMENT(g_value_dup_object(value));
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void custom_media_factory_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    CustomMediaFactory *factory = CUSTOM_MEDIA_FACTORY(object);

    switch (prop_id) {
        case 1:
            //g_value_set_object(value, factory->my_bin);
            gst_rtsp_media_factory_custom_set_bin (factory, g_value_get_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

// 클래스 초기화
static void custom_media_factory_class_init(CustomMediaFactoryClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->set_property = custom_media_factory_set_property;
    gobject_class->get_property = custom_media_factory_get_property;
    g_object_class_install_property(gobject_class, 1,
                                    g_param_spec_object("my-bin", "My Bin", "My custom bin", GST_TYPE_ELEMENT,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

// 인스턴스 초기화
static void custom_media_factory_init(CustomMediaFactory *factory) {
    factory->my_bin = NULL;
}
#endif

volatile int interrupted = 0; // caught signals will be stored here
void terminateSignalHandler(int sig)
{
    static int timesCalled = 1;
    if (timesCalled > 1)
    {
        g_print("Interrupted again, exitting rudely!\n"); 
        exit(EXIT_FAILURE);
    }
    interrupted = sig;
    timesCalled++;
}

void attachInterruptHandlers()
{
    // attach interrupt handlers
    signal(SIGINT, &terminateSignalHandler);
    signal(SIGTERM, &terminateSignalHandler);
}

typedef struct _Data {
    GstRTSPServer *server;
    GgstLoop *loop;
}Data;

void cleanSessions(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);
}
/* this timeout is periodically run to clean up the expired sessions from the
 * pool. This needs to be run explicitly currently but might be done
 * automatically as part of the gstLoop. */
gboolean timeout(Data *data)
{
  gboolean result = FALSE;
  if (interrupted)
  {
      g_print("Interrupted, quitting...\n");
      if (data->loop)
          g_main_loop_quit(data->loop);
  }
  else
  {
      cleanSessions(data->server);
      result = TRUE;
  }
  return result;
}

gboolean timeoutTest(GstElement *pipeline)
{
    g_message("timeoutTest");
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    g_message("GST_STATE_PAUSED");

    return 1;
}
#if 0
GstElement *get_bin_from_rtsp_server(GstRTSPServer *server, const gchar *mount_path) {
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMedia *media = gst_rtsp_mount_points_lookup(mounts, mount_path);

    if (media) {
        GstRTSPMediaFactory *factory = gst_rtsp_media_get_factory(media);
        g_object_unref(media);

        if (factory) {
            return gst_rtsp_media_factory_get_element(factory);
        }
    }

    return NULL;
}
#endif
int main(int argc, char *argv[]) {
    attachInterruptHandlers();
    //GgstLoop *loop;
    //GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    //CustomMediaFactory *factory;
    GstRTSPMediaFactory *factory;
    gchar *rtsp_url;
    GstRTSPUrl *url;
    Data data;

    if(gst_rtsp_url_parse ("rtsp://localhost:8554/test", &url) != GST_RTSP_OK) {
        g_error("gst_rtsp_url_parse failed");;
    }

    gst_init(&argc, &argv);
    data.loop = g_main_loop_new(NULL, FALSE);
    
    // RTSP 서버 생성
    data.server = gst_rtsp_server_new();
    g_object_set (data.server, "service", "8554", NULL);
    mounts = gst_rtsp_server_get_mount_points(data.server);

    // 사용자 정의 미디어 팩토리 생성
    //factory = g_object_new(CUSTOM_TYPE_MEDIA_FACTORY, "my-bin", NULL, NULL);
    factory = gst_rtsp_media_factory_new ();

    //gchar *launchLine = NULL;
    //launchLine = malloc(32 * 1024);
    gchar launchLine[1024];
	sprintf(launchLine, "(videotestsrc ! vpuenc_h264 ! rtph264pay name=pay0 pt=96)");
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(launchLine, &error);
    gst_rtsp_media_factory_set_launch(factory, "( v4l2src device=/dev/video3 ! vpuenc_h264 ! h264parse ! rtph264pay name=pay0 pt=96 )");
    //factory->my_bin = gst_parse_bin_from_description("(videotestsrc ! vpuenc_h264 ! rtph264pay name=pay0 pt=96)", TRUE, NULL);
    //g_object_set(factory, "my-bin", pipeline, NULL);
    // RTSP 미디어 팩토리에 사용자 정의 미디어 팩토리 설정
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);

    pipeline = gst_rtsp_media_factory_create_element (factory, url);
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(pipeline));

    // RTSP 서버 시작
    gst_rtsp_server_attach(data.server, NULL);
    //gst_element_set_state(pipeline, GST_STATE_PLAYING);

    rtsp_url = gst_rtsp_server_get_address(data.server);

    g_print("RTSP 서버 시작: %s/test, %s\n", rtsp_url, url);

    g_free(rtsp_url);

    //gst_element_set_state(GST_ELEMENT(factory), GST_STATE_PLAYING);
    //GstBus *bus = gst_element_get_bus(pipeline);
    //guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    //gst_object_unref(bus);
    //GstElement *bin = get_bin_from_rtsp_server(data.server, "/test");

    g_timeout_add_seconds(1, (GSourceFunc) timeout, &data);
    //g_timeout_add_seconds(30, (GSourceFunc) timeoutTest, bin);
    
    // 두 파이프라인 재생 시작
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // 메인 루프 실행
    g_main_loop_run(data.loop);

    // RTSP 서버 해제
    //gst_rtsp_server_unattach(server);
    g_object_unref(data.server);

    return 0;
}