#include <gst/gst.h>

GMainLoop *loop;
GstElement *pipeline;

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

static gboolean sendEOS(gpointer data) 
{
    //CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;

    gst_element_send_event(pipeline, gst_event_new_eos());

    return TRUE;
}

int main(int argc, char *argv[]) {
    // Initialize GStreamer
    gst_init(&argc, &argv);
    attachInterruptHandlers();

    GstElement *pipeline, *filesrc_video, *filesrc_audio, *demux_video, *demux_audio,
               *queue_video, *queue_audio, *h264parse, *aacparse, *mux, *filesink;

    // Create GStreamer elements
    pipeline = gst_pipeline_new("merger_pipeline");
    filesrc_video = gst_element_factory_make("filesrc", "filesrc_video");
    filesrc_audio = gst_element_factory_make("filesrc", "filesrc_audio");
    demux_video = gst_element_factory_make("qtdemux", "demux_video");
    demux_audio = gst_element_factory_make("qtdemux", "demux_audio");
    queue_video = gst_element_factory_make("queue", "queue_video");
    queue_audio = gst_element_factory_make("queue", "queue_audio");
    h264parse = gst_element_factory_make("h264parse", "h264parse");
    aacparse = gst_element_factory_make("aacparse", "aacparse");
    mux = gst_element_factory_make("mp4mux", "mux");
    filesink = gst_element_factory_make("filesink", "filesink");

    if (!pipeline || !filesrc_video || !filesrc_audio || !demux_video || !demux_audio ||
        !queue_video || !queue_audio || !h264parse || !aacparse || !mux || !filesink) {
        g_error("One or more elements could not be created. Exiting.\n");
        return -1;
    }

    // Set file paths for video and audio files
    g_object_set(G_OBJECT(filesrc_video), "location", "output_video.mp4", NULL);
    g_object_set(G_OBJECT(filesrc_audio), "location", "output_audio.mp3", NULL);

    // Set output file path
    g_object_set(G_OBJECT(filesink), "location", "merged.mp4", NULL);

    // Build the pipeline
    gst_bin_add_many(GST_BIN(pipeline), filesrc_video, filesrc_audio, demux_video, demux_audio,
                     queue_video, queue_audio, h264parse, aacparse, mux, filesink, NULL);
#if 0
    if (!gst_element_link(filesrc_video, demux_video) || !gst_element_link(filesrc_audio, demux_audio) ||
        !gst_element_link(demux_video, queue_video) || !gst_element_link(demux_audio, queue_audio) ||
        !gst_element_link(queue_video, h264parse) || !gst_element_link(queue_audio, aacparse) ||
        !gst_element_link_many(h264parse, mux, filesink, NULL)) {
        g_error("Elements could not be linked. Exiting.\n");
        return -1;
    }
#endif
#if 0
    if (!gst_element_link(filesrc_video, demux_video) || 
        !gst_element_link(demux_video, queue_video) ||
        !gst_element_link(queue_video, h264parse) ||
        !gst_element_link_many(h264parse, mux, filesink, NULL)) {
        g_error("Elements could not be linked. Exiting.\n");
        return -1;
    }
#endif
#if 0
    if (!gst_element_link(filesrc_audio, demux_audio)) {
        g_error("Elements could not be linked1. Exiting.\n");
        return -1;
    }
    if (!gst_element_link(demux_audio, queue_audio)) {
        g_error("Elements could not be linked2. Exiting.\n");
        return -1;
    }
    if (!gst_element_link(queue_audio, aacparse)) {
        g_error("Elements could not be linked3. Exiting.\n");
        return -1;
    }
    if (!gst_element_link_many(aacparse, mux, filesink, NULL)) {
        g_error("Elements could not be linked4. Exiting.\n");
        return -1;
    }
#endif
    if (!gst_element_link_many(filesrc_audio, queue_audio, mux, filesink))
    {
        g_error("Elements could not be linked5. Exiting.\n");
        return -1;
    }

    // Set properties for fileSink element

    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstBus *bus = gst_element_get_bus(pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);

    loop = g_main_loop_new(NULL, FALSE);

    g_print("Running...\n");
    g_timeout_add_seconds(60, (GSourceFunc)sendEOS, NULL);

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



