
#include <stdio.h>
#include <gst/gst.h>
//#include <glib.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>

#define GST_DEUBG=0
#define LEFT_TESTx
#define RIGHT_TEST

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
    GstElement *rtspsrc_right;
    GstCaps *caps;
} CustomData;

typedef struct {
  gboolean white;
  GstClockTime timestamp;
} MyContext;

static CustomData data;
static GgstLoop *loop;
pthread_t m_threadRtsp;
pthread_t m_threadRtsp2;
GstElement *rtspsrc_left;
GstElement *rtspsrc_right;

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

#if 0
/* called when a new media pipeline is constructed. We can query the
 * pipeline and configure our appsrc */
static void media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media, gpointer user_data)
{
  GstElement *element, *appsrc;
  MyContext *ctx;

  /* get the element used for providing the streams of the media */
  element = gst_rtsp_media_get_element (media);

  /* get our appsrc, we named it 'mysrc' with the name property */
  appsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "mysrc");

  /* this instructs appsrc that we will be dealing with timed buffer */
  gst_util_set_object_arg (G_OBJECT (appsrc), "format", "time");
  /* configure the caps of the video */
  g_object_set (G_OBJECT (appsrc), "caps",
      gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "RGB16",
          "width", G_TYPE_INT, 384,
          "height", G_TYPE_INT, 288,
          "framerate", GST_TYPE_FRACTION, 0, 1, NULL), NULL);

  ctx = g_new0 (MyContext, 1);
  ctx->white = FALSE;
  ctx->timestamp = 0;
  /* make sure ther datais freed when the media is gone */
  g_object_set_data_full (G_OBJECT (media), "my-extra-data", ctx,
      (GDestroyNotify) g_free);

  /* install the callback that will be called when a buffer is needed */
  g_signal_connect (appsrc, "need-data", (GCallback) need_data, ctx);
  gst_object_unref (appsrc);
  gst_object_unref (element);
}
#else
static void	media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
	//GstElement *src = (GstElement*)user_data;
    CustomData *arg = (CustomData*)user_data;
	GstElement *element;
    static GMutex mutex;

	g_mutex_lock(&mutex);
    g_print("media_configure");
	element = gst_rtsp_media_get_element(media);
	arg->rtspsrc_left = gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtspsrc_left");

	gst_object_unref(element);

	g_mutex_unlock(&mutex);
}

static void	media_configure2(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
	//GstElement *src = (GstElement*)user_data;
    CustomData *arg = (CustomData*)user_data;
	GstElement *element;
    static GMutex mutex;

	g_mutex_lock(&mutex);
    g_print("media_configure");
	element = gst_rtsp_media_get_element(media);
	arg->rtspsrc_right = gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtspsrc_right");

	gst_object_unref(element);

	g_mutex_unlock(&mutex);
}
#endif

static gboolean timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
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

void* threadRtsp()
{
  GgstLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  const gchar *mount_path = "/test";
  gchar *rtsp_url;
  //gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  g_object_set (server, "service", "8554", NULL);
  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, "( appsrc name=rtspsrc_left ! queue ! h264parse ! rtph264pay name=pay0 pt=96 )");
  //gst_rtsp_media_factory_set_launch (factory, 
  //"( appsrc name=rtspsrc_left max-bytes=0 do-timestamp=1 is-live=1 format=3 \
  //! queue max-size-buffers=10 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )");
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  /* notify when our media is ready, This is called whenever someone asks for
   * the media and a new pipeline with our appsrc is created */
  g_signal_connect (factory, "media-configure", (GCallback) media_configure, &data);
  //g_signal_connect (factory, "media-constructed ", (GCallback) media_constructed, &data);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, mount_path, factory);

  /* don't need the ref to the mounts anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  rtsp_url = gst_rtsp_server_get_address(server);
  g_print ("stream ready at rtsp://%s%s\n", rtsp_url, mount_path);

  //GstElement *bin = get_bin_from_rtsp_server(server, mount_path);

  g_main_loop_run (loop);

  return NULL;
}

void* threadRtsp2()
{
  GgstLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;

  //gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  g_object_set (server, "service", "8554", NULL);
  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  //gst_rtsp_media_factory_set_launch (factory,"( appsrc name=rtspsrc_right ! queue ! h264parse ! rtph264pay name=pay0 pt=96 )");
  gst_rtsp_media_factory_set_launch (factory, 
  "( appsrc name=rtspsrc_right max-bytes=0 do-timestamp=1 is-live=1 format=3 \
  ! queue max-size-buffers=10 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )");
  gst_rtsp_media_factory_set_shared(factory, TRUE);

  /* notify when our media is ready, This is called whenever someone asks for
   * the media and a new pipeline with our appsrc is created */
  g_signal_connect (factory, "media-configure", (GCallback) media_configure2, &data);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test2", factory);

  /* don't need the ref to the mounts anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);
  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:8554/test2\n");
  g_main_loop_run (loop);

  return NULL;

failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}

/* The appsink has received a buffer */
static GstFlowReturn new_sample (GstElement * sink, gpointer user_data)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstMapInfo map;
  static GMutex mutex;
  guint8 *data;
  gsize size;
  GstCaps *caps;
  GstPad *pad;
  //GstElement *src = (GstElement*)user_data;
  CustomData *arg = (CustomData*)user_data;
  /* Retrieve the buffer */

  if( (arg == NULL) || (sink == NULL) )
  {
    g_print ("not set rtspsink_left\n");
	return GST_FLOW_ERROR;
  }
#if 0
  if(arg->rtspsrc_left == NULL)
  {
    g_print ("not yet rtspsrc_left\n");
	return GST_FLOW_ERROR;
  }
  if(GST_STATE(arg->rtspsrc_left) == GST_STATE_NULL || GST_STATE(arg->rtspsrc_left) == GST_STATE_READY)
  {
    g_print ("not yet rtspsrc_left\n");
	return GST_FLOW_ERROR;
  }
#endif
  g_mutex_lock(&mutex);
  sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  if (sample) {
    buffer = gst_sample_get_buffer(sample);
    if (buffer) {
        // appsrc로 데이터 전송
        gst_app_src_push_buffer(GST_APP_SRC(arg->rtspsrc_left), gst_buffer_copy(buffer));
    }
    gst_sample_unref(sample);
  }
  g_mutex_unlock(&mutex);
  return GST_FLOW_OK;
}

static GstFlowReturn new_sample2 (GstElement * sink, gpointer user_data)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstMapInfo map;
  static GMutex mutex;
  guint8 *data;
  gsize size;
  GstCaps *caps;
  GstPad *pad;
  //GstElement *src = (GstElement*)user_data;
  CustomData *arg = (CustomData*)user_data;
  /* Retrieve the buffer */
  if( (arg == NULL) || (sink == NULL) )
  {
    g_print ("not set rtspsink_right\n");
	return GST_FLOW_ERROR;
  }
#if 0
  if(arg->rtspsrc_left == NULL)
  {
    g_print ("not yet rtspsrc_right\n");
	return GST_FLOW_ERROR;
  }
#endif
  g_mutex_lock(&mutex);
  sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  if (sample) {
    buffer = gst_sample_get_buffer(sample);
    if (buffer) {
        // appsrc로 데이터 전송
        gst_app_src_push_buffer(GST_APP_SRC(arg->rtspsrc_right), gst_buffer_copy(buffer));
    }
    gst_sample_unref(sample);
  }
  g_mutex_unlock(&mutex);
  return GST_FLOW_ERROR;
}

int main (int argc, char *argv[])
{
    const gchar *nano_str;
    guint major, minor, micro, nano;
    //CustomData data;
    GstPad *tee_left_pad;
    GstPad *tee_right_pad;
    GstPad *queue_left_pad;
    GstPad *queue_right_pad;

    signal(SIGINT, sigintHandler);
    signal(SIGKILL, sigkillHandler);
    signal(SIGTERM, sigtermHandler);
    
    gst_init(&argc, &argv);
    gst_version(&major, &minor, &micro, &nano);

    if(nano == 1)
        nano_str="(CVS)";
    else if(nano ==2)
        nano_str="(Prerelease)";
    else
        nano_str="";

    printf("This program i linked against Gstreamer %d.%d.%d %s\n", major, minor, micro, nano_str);

    gboolean silent = FALSE;
    gchar *savefile = NULL;
    GOptionContext *ctx;
    GError *err = NULL;
    GOptionEntry entries[] = {
        {"silent", 's', 0, G_OPTION_ARG_NONE, &silent, "do not output status information", NULL},
        {"output", 'o', 0, G_OPTION_ARG_STRING, &savefile, "save xml representation of pipeline to FILE and exit", "FILE"},
        {NULL}
    };

    ctx = g_option_context_new("- Your application");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());

    if(!g_option_context_parse(ctx, &argc, &argv, &err))
    {
        printf("Failed to initialize: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    printf("Run me with --help to see the Application options appended.\n");

#if 0
    GstElement *element;
    gchar *name;

    element = gst_element_factory_make("fakesrc", "source");
    g_object_get(G_OBJECT(element), "name", &name, NULL);
    printf("The name of the element is %s\n", name);
    g_free(name);
    gst_object_unref(GST_OBJECT(element));

    GstElementFactory *factory;

    factory = gst_element_factory_find("fakesrc");
    if(!factory) {
        printf("You don't have the 'fakesrc' element installed!\n");
    }

    printf("The '%s' element is a memeber of the category %s\n" "Description : %s\n", 
            gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),
            gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS),
            gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_DESCRIPTION)
            );

    GstElement *pipeline, *bin;
    GstElement *source, *filter, *sink;

    pipeline = gst_pipeline_new("my-pipeline");
    bin = gst_bin_new("my-bin");
    source = gst_element_factory_make("fakesrc", "source");
    filter = gst_element_factory_make("identity", "filter");
    sink = gst_element_factory_make("fakesink", "sink");
#endif

#if 0
    gst_bin_add_many(GST_BIN(pipeline), source, filter, sink, NULL);
    if(!gst_element_link_many(source, filter, sink, NULL)) {
        printf("Failed to link elements!");
    }
#endif

#if 0
    gst_bin_add_many(GST_BIN(bin), source, filter, sink, NULL);

    gst_bin_add(GST_BIN(pipeline), bin);

    gst_element_link(source, sink);
#endif

#if 0
    GstElement *player;

    player = gst_element_factory_make("playbin", "player");
    g_object_set(player, "uri", "file:///home/user/1.mp4", NULL);

    gst_element_set_state(GST_ELEMENT(player), GST_STATE_PLAYING);
#endif

#if 0
    GstElement *player;
    GstBus *bus;
    guint bus_watch_id;

    pipeline = gst_pipeline_new("my-pipeline");

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    //gst_bus_add_signal_watch(bus);
    //g_signal_connect(bus, "message::state-changed", G_CALLBACK(cb_message_error), NULL);

    gst_object_unref(bus);

    player = gst_element_factory_make("playbin", "player");
    g_object_set(player, "uri", "file:///home/user/1.mp4", NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    printf("GST_STATE_PLAYING\n");

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    printf("GST_STATE_NULL\n");
    gst_object_unref(pipeline);
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);
#endif

#if 0
    GstElement *pipeline, *source, *demux;
    GgstLoop *loop;
    GstBus *bus;
    guint bus_watch_id;

    pipeline = gst_pipeline_new("my-pipeline");
    source = gst_element_factory_make("filesrc", "source");
    g_object_set(source, "location", argv[1], NULL);
    //source = gst_element_factory_make("souphttpsrc", "source");
    //g_object_set(source, "location", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
    demux = gst_element_factory_make("oggdemux", "demuxer");

    gst_bin_add_many(GST_BIN(pipeline), source, demux, NULL);
    //link_to_multiplexer()
    gst_element_link_pads(source, "src", demux, "sink");

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    //gst_bus_add_signal_watch(bus);
    //g_signal_connect(bus, "message::state-changed", G_CALLBACK(cb_message_error), NULL);
    gst_object_unref(bus);

    g_signal_connect(demux, "pad-added", G_CALLBACK(cb_new_pad), NULL);

    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
#endif

    GstBus *bus;
    guint bus_watch_id;

    //source = gst_element_factory_make("videotestsrc", "source");
    data.source = gst_element_factory_make("v4l2src", "source");
    data.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    data.tee_crop = gst_element_factory_make("tee", "tee_crop");
    data.tee_left = gst_element_factory_make("tee", "tee_left");
    data.tee_right = gst_element_factory_make("tee", "tee_right");
    data.crop_left = gst_element_factory_make("videocrop", "crop_left");
    data.crop_right = gst_element_factory_make("videocrop", "crop_right");
    data.queue_left = gst_element_factory_make("queue", "queue_left");
    data.queue_right = gst_element_factory_make("queue", "queue_right");
#if 1
    data.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    data.encoder_file_left = gst_element_factory_make("vpuenc_h264", "encoder_file_left");
    data.encoder_file_right = gst_element_factory_make("vpuenc_h264", "encoder_file_right");
    data.encoder_rtsp_left = gst_element_factory_make("vpuenc_h264", "encoder_rtsp_left");
    data.encoder_rtsp_right = gst_element_factory_make("vpuenc_h264", "encoder_rtsp_right");
#else
    encoder_file = gst_element_factory_make("x264enc", "encoder_file");
    encoder_rtsp = gst_element_factory_make("x264enc", "encoder_rtsp");
#endif
    data.queue_file_left = gst_element_factory_make("queue", "queue_file_left");
    data.queue_file_right = gst_element_factory_make("queue", "queue_file_right");
    data.queue_rtsp_left = gst_element_factory_make("queue", "queue_rtsp_left");
    data.queue_rtsp_right = gst_element_factory_make("queue", "queue_rtsp_right");
    data.parse_file_left = gst_element_factory_make("h264parse", "parse_file_left");
    data.parse_file_right = gst_element_factory_make("h264parse", "parse_file_right");
    data.parse_rtsp_left = gst_element_factory_make("h264parse", "parse_rtsp_left");
    data.parse_rtsp_right = gst_element_factory_make("h264parse", "parse_rtsp_right");
    data.filesink_left = gst_element_factory_make("splitmuxsink", "filesink_left");
    data.filesink_right = gst_element_factory_make("splitmuxsink", "filesink_right");
    //data.rtspsink_left = gst_element_factory_make("splitmuxsink", "rtspsink_left");
    //data.rtspsink_right = gst_element_factory_make("splitmuxsink", "rtspsink_right");
    data.rtspsink_left = gst_element_factory_make("appsink", "rtspsink_left");
    data.rtspsink_right = gst_element_factory_make("appsink", "rtspsink_right");
    //sink = gst_element_factory_make("filesink", "sink");

    if(!data.source || !data.convert || !data.capsfilter || !data.tee_crop || !data.crop_left || !data.crop_right \
        || !data.queue_left || !data.queue_right || !data.encoder_file_left || !data.encoder_file_right \
        || !data.encoder_rtsp_left || !data.encoder_rtsp_right || !data.parse_file_left || !data.parse_file_right || !data.parse_rtsp_left \
        || !data.parse_rtsp_right || !data.filesink_left || !data.filesink_right || !data.rtspsink_left || !data.rtspsink_right \
        || !data.tee_left || !data.tee_right || !data.queue_file_left || !data.queue_file_right || !data.queue_rtsp_left || !data.queue_rtsp_right
        )
    {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    data.pipeline = gst_pipeline_new("my-pipeline");

    g_object_set(data.source, "device", "/dev/video3", NULL);
    g_object_set(data.crop_left, "top", 0, "bottom", 0, "left", 1920, "right", 0, NULL);
    g_object_set(data.crop_right, "top", 0, "bottom", 0, "left", 0, "right", 1920, NULL);
    g_object_set(data.encoder_file_left, "bitrate", 4096, NULL);
    g_object_set(data.encoder_file_right, "bitrate", 4096, NULL);
    g_object_set(data.encoder_rtsp_left, "bitrate", 1024, NULL);
    g_object_set(data.encoder_rtsp_right, "bitrate", 1024, NULL);
    g_object_set(data.filesink_left, "location", "fl%02d.mp4", "max-size-time", 60000000000, NULL);
    g_object_set(data.filesink_right, "location", "fr%02d.mp4", "max-size-time", 60000000000, NULL);
    //g_object_set(data.rtspsink_left, "location", "rl%02d.mp4", "max-size-time", 60000000000, NULL);
    //g_object_set(data.rtspsink_right, "location", "rr%02d.mp4", "max-size-time", 60000000000, NULL);
    g_object_set(data.rtspsink_left, "name", "rtspsink_left", NULL);
    g_object_set(data.rtspsink_right, "name", "rtspsink_right", NULL);

#if 0
    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.capsfilter, data.crop_left, data.queue_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, \
                    data.tee_crop, data.queue_right, data.crop_right, data.encoder_file_right, data.encoder_rtsp_left, data.encoder_rtsp_right, \
                    data.parse_file_right, data.parse_rtsp_left, data.parse_rtsp_right, data.queue_rtsp_right, data.queue_rtsp_left, data.tee_left, \
                    data.tee_right, data.queue_file_left, data.queue_file_right, NULL);

    if(!gst_element_link_many(data.source, data.convert, data.capsfilter, data.crop_left, data.queue_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, NULL) )
    {
        g_printerr("Main Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

#endif

#if 0
    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.capsfilter, data.crop_left, data.queue_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, \
                    data.tee_crop, data.queue_right, data.crop_right, data.encoder_file_right, data.encoder_rtsp_left, data.encoder_rtsp_right, \
                    data.parse_file_right, data.parse_rtsp_left, data.parse_rtsp_right, data.queue_rtsp_right, data.queue_rtsp_left, data.tee_left, \
                    data.tee_right, data.queue_file_left, data.queue_file_right, data.rtspsink_right, NULL);

    if(!gst_element_link_many(data.source, data.convert, data.capsfilter, data.tee_crop, NULL) 
        || !gst_element_link_many(data.tee_crop, data.queue_left, data.crop_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, NULL)
        || !gst_element_link_many(data.tee_crop, data.queue_right, data.crop_right, data.encoder_rtsp_right, data.parse_rtsp_right, data.rtspsink_right, NULL)
        )
    {
        g_printerr("Main Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#endif

    //gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.capsfilter, data.tee_crop, data.queue_left, data.crop_left,
    //                data.encoder_file_left, data.encoder_rtsp_left, data.parse_file_left, data.parse_rtsp_left, data.filesink_left, 
    //                data.rtspsink_left, data.tee_left, data.queue_file_left, data.queue_rtsp_left, 
    //                NULL);

    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.capsfilter, data.tee_crop, NULL);

    if(!gst_element_link_many(data.source, data.convert, data.capsfilter, data.tee_crop, NULL))
    {
        g_printerr("Main Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#ifdef LEFT_TEST
    gst_bin_add_many(GST_BIN(data.pipeline), data.queue_left, data.crop_left, data.tee_left, data.filesink_left, data.rtspsink_left, 
                    data.queue_file_left, data.encoder_file_left, data.parse_file_left, data.queue_rtsp_left, data.encoder_rtsp_left, data.parse_rtsp_left, 
                    NULL);

    if(!gst_element_link_many(data.tee_crop, data.queue_left, data.crop_left, data.tee_left, NULL))
    {
        g_printerr("Left tee Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    if(!gst_element_link_many(data.tee_left, data.queue_file_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, NULL)
        || !gst_element_link_many(data.tee_left, data.queue_rtsp_left, data.encoder_rtsp_left, data.parse_rtsp_left, data.rtspsink_left, NULL)
        )
    {
        g_printerr("Left sink could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    //rtspsrc_left = gst_element_factory_make("appsrc", "rtspsrc_left");
    //g_object_set(rtspsrc_left, "name", "rtspsrc_left", NULL);
#if 0
    gst_bin_add(GST_BIN(data.pipeline), rtspsrc_left);
    if(!gst_element_link(data.rtspsink_left, rtspsrc_left))
    {
        g_printerr("Left src Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#endif
	g_signal_connect(G_OBJECT(data.rtspsink_left), "new-sample", G_CALLBACK (new_sample), &data);
    
	// set object app sink
	g_object_set(data.rtspsink_left, "sync", FALSE, NULL);
	g_object_set(data.rtspsink_left, "emit-signals", TRUE, NULL);

	// Add
	g_object_set(data.rtspsink_left, "max-buffers", 60, NULL);
	g_object_set(data.rtspsink_left, "drop", FALSE, NULL);

    pthread_create(&m_threadRtsp, NULL, &threadRtsp, NULL);
    //sleep(1);
#endif

#ifdef RIGHT_TEST
    gst_bin_add_many(GST_BIN(data.pipeline), data.queue_right, data.crop_right, data.tee_right, data.filesink_right, data.rtspsink_right, 
                    data.queue_file_right, data.encoder_file_right, data.parse_file_right, data.queue_rtsp_right, data.encoder_rtsp_right, data.parse_rtsp_right, 
                    NULL);
    if(!gst_element_link_many(data.tee_crop, data.queue_right, data.crop_right, data.tee_right, NULL))
    {
        g_printerr("Right tee Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if(!gst_element_link_many(data.tee_right, data.queue_file_right, data.encoder_file_right, data.parse_file_right, data.filesink_right, NULL)
        || !gst_element_link_many(data.tee_right, data.queue_rtsp_right, data.encoder_rtsp_right, data.parse_rtsp_right, data.rtspsink_right, NULL)
        )
    {
        g_printerr("Right sink Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#if 0
    rtspsrc_right = gst_element_factory_make("appsrc", "rtspsrc_right");
    g_object_set(rtspsrc_right, "name", "rtspsrc_right", NULL);
    gst_bin_add(GST_BIN(data.pipeline), rtspsrc_right);
    if(!gst_element_link(data.rtspsink_right, rtspsrc_right))
    {
        g_printerr("Right src Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#endif
    g_signal_connect(G_OBJECT(data.rtspsink_right), "new-sample", G_CALLBACK (new_sample2), &data);

	// set object app sink
	g_object_set(data.rtspsink_right, "sync", FALSE, NULL);
	g_object_set(data.rtspsink_right, "emit-signals", TRUE, NULL);

	// Add
	g_object_set(data.rtspsink_right, "max-buffers", 60, NULL);
	g_object_set(data.rtspsink_right, "drop", FALSE, NULL);

    pthread_create(&m_threadRtsp2, NULL, &threadRtsp2, NULL);
#endif

#if 0
    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.capsfilter, data.tee_crop, data.queue_left, data.crop_left, data.queue_right,
                    data.crop_right, data.encoder_file_left, data.encoder_file_right, data.encoder_rtsp_left, data.encoder_rtsp_right,
                    data.parse_file_left, data.parse_file_right, data.parse_rtsp_left, data.parse_rtsp_right, data.filesink_left, data.filesink_right,
                    data.rtspsink_left, data.rtspsink_right, data.tee_left, data.tee_right, data.queue_file_left, data.queue_file_right,
                    data.queue_rtsp_left, data.queue_rtsp_right, NULL);

    if(!gst_element_link_many(data.source, data.convert, data.capsfilter, data.tee_crop, NULL) 
        || !gst_element_link_many(data.tee_crop, data.queue_left, data.crop_left, data.tee_left, NULL)
        || !gst_element_link_many(data.tee_crop, data.queue_right, data.crop_right, data.tee_right, NULL)
        )
    {
        g_printerr("Main Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if(!gst_element_link_many(data.tee_left, data.queue_file_left, data.encoder_file_left, data.parse_file_left, data.filesink_left, NULL)
        || !gst_element_link_many(data.tee_left, data.queue_rtsp_left, data.encoder_rtsp_left, data.parse_rtsp_left, data.rtspsink_left, NULL)
        )
    {
        g_printerr("Left Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    if(!gst_element_link_many(data.tee_right, data.queue_file_right, data.encoder_file_right, data.parse_file_right, data.filesink_right, NULL)
        || !gst_element_link_many(data.tee_right, data.queue_rtsp_right, data.encoder_rtsp_right, data.parse_rtsp_right, data.rtspsink_right, NULL)
        )
    {
        g_printerr("Right Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
#endif

#if 0
    tee_left_pad = gst_element_get_request_pad(data.tee_crop, "src_%u");
    tee_right_pad = gst_element_get_request_pad(data.tee_crop, "src_%u");
    g_print ("Obtained request pad %s for left branch.\n", gst_pad_get_name (tee_left_pad));
    g_print ("Obtained request pad %s for right branch.\n", gst_pad_get_name (tee_right_pad));
    queue_left_pad = gst_element_get_static_pad (data.queue_left, "sink");
    queue_right_pad = gst_element_get_static_pad (data.queue_right, "sink");

    if (gst_pad_link (tee_left_pad, queue_left_pad) != GST_PAD_LINK_OK 
        || gst_pad_link (tee_right_pad, queue_right_pad) != GST_PAD_LINK_OK
        ) 
    {
        g_printerr ("Tee could not be linked\n");
        gst_object_unref (data.pipeline);
        return -1;
    }
    gst_object_unref (queue_left_pad);
    gst_object_unref (queue_right_pad);
#endif

	data.caps = gst_caps_new_simple("video/x-raw",
			"width", G_TYPE_INT, 3840, "height", G_TYPE_INT, 1080,
			"framerate", GST_TYPE_FRACTION, 30, 1, NULL);

	if(data.caps < 0)
    {
        g_printerr("caps error.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    g_object_set(data.capsfilter, "caps", data.caps, NULL);
    gst_caps_unref(data.caps);

    //sleep(1);
    //g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    bus = gst_element_get_bus(data.pipeline);
    bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);
   
    //gst_bus_add_signal_watch(bus);
    //g_signal_connect(bus, "message::state-changed", G_CALLBACK(cb_message_error), NULL);
    
    loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(loop);

#if 1
    g_main_loop_unref(loop);
    gst_element_set_state (data.pipeline, GST_STATE_NULL);
    //gst_element_release_request_pad (data.tee_left, tee_left_pad);
    //gst_element_release_request_pad (data.tee_right, tee_right_pad);
    //gst_object_unref (tee_left_pad);
    //gst_object_unref (tee_right_pad);
    gst_object_unref(data.pipeline);
#endif

    return 0;
}
