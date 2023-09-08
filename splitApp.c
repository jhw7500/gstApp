#include <gst/gst.h>

GstElement *pipeline;
GMainLoop *loop;

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
            g_print("End of stream\n");
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
#if 0
            GstTagList *tags = NULL;
            gst_message_parse_tag (message, &tags);
            g_print ("Got tags from element %s\n", GST_OBJECT_NAME (message->src));
            gst_tag_list_foreach (tags, print_tag, NULL);
            gst_tag_list_free (tags);
            //handle_tags (tags);
            gst_tag_list_unref (tags);
#endif
            break;
        }

        default:
            break;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    attachInterruptHandlers();

    GstElement *videotestsrc, *videoconvert, *capsfilter, *x264enc, *queue_video;
    GstElement *audiotestsrc, *audioconvert, *audioresample, *voaacenc, *queue_audio, *parse;
    GstElement *muxer, *splitmuxsink;

    pipeline = gst_pipeline_new("video-audio-pipeline");
    videotestsrc = gst_element_factory_make("videotestsrc", "video-source");
    videoconvert = gst_element_factory_make("videoconvert", "video-convert");
    capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
    x264enc = gst_element_factory_make("vpuenc_h264", "video-encoder");
    parse = gst_element_factory_make("h264parse", "h264parse");
    queue_video = gst_element_factory_make("queue", "video-queue");
    audiotestsrc = gst_element_factory_make("audiotestsrc", "audio-source");
    audioconvert = gst_element_factory_make("audioconvert", "audio-convert");
    audioresample = gst_element_factory_make("audioresample", "audio-resample");
    voaacenc = gst_element_factory_make("lamemp3enc", "audio-encoder");
    queue_audio = gst_element_factory_make("queue", "audio-queue");
    muxer = gst_element_factory_make("mp4mux", "mp4-muxer");
    splitmuxsink = gst_element_factory_make("splitmuxsink", "split-muxer");

    if (!pipeline || !videotestsrc || !videoconvert || !capsfilter || !x264enc ||
        !queue_video || !audiotestsrc || !audioconvert || !audioresample ||
        !voaacenc || !queue_audio || !muxer || !splitmuxsink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    // Set properties for video elements
    //g_object_set(videotestsrc, "do-timestamp", TRUE, NULL);
    g_object_set(videotestsrc, "is-live", TRUE, NULL);
    g_object_set(capsfilter, "caps", gst_caps_from_string("video/x-raw,format=I420"), NULL);
    //g_object_set(x264enc, "bitrate", 2000000, NULL);

    // Set properties for audio elements
    g_object_set(audiotestsrc, "is-live", TRUE, NULL);

    // Set properties for splitmuxsink
#if 0
    g_object_set(splitmuxsink,
                 "muxer", gst_element_factory_make("mp4mux", "muxer"),
                 "location", "output_%02d.mp4",
                 "max-size-time", G_GUINT64_CONSTANT(10000000000), // 1 minute in nanoseconds
                 NULL);
#endif


    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), videotestsrc, videoconvert, capsfilter, x264enc, parse, queue_video, splitmuxsink, NULL);
    //gst_bin_add_many(audiotestsrc, audioconvert, audioresample, voaacenc, queue_audio, muxer, NULL);

    g_object_set(G_OBJECT(splitmuxsink), "muxer", muxer, NULL);
    g_object_set(G_OBJECT(splitmuxsink), "location", "tost%05d.mp4", NULL);
    g_object_set(G_OBJECT(splitmuxsink), "max-size-time", 10000000000, NULL);

    // Link video elements
    gst_element_link_many(videotestsrc, x264enc, parse, splitmuxsink, NULL);

    // Request the muxer's video_0 pad
    GstPad *muxer_pad, *sink_pad;

    // Link audio elements
    //gst_element_link_many(audiotestsrc, audioconvert, audioresample, voaacenc, queue_audio, splitmuxsink, NULL);

    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

#if 0
    // Run the pipeline
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (msg != NULL) {
        // Handle messages if needed
        gst_message_unref(msg);
    }
#endif
    GstBus *bus = gst_element_get_bus(pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);

    loop = g_main_loop_new(NULL, FALSE);

    g_print("Running...\n");
    g_main_loop_run(loop);

    // Stop the pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    return 0;
}