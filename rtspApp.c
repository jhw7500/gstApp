#include <stdio.h>
#include <gst/gst.h>
#include <microhttpd.h>

static GstElement *pipeline;
static struct MHD_Daemon *http_daemon;

static int http_request_handler(void *cls, struct MHD_Connection *connection, const char *url,
                                const char *method, const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
    if (strcmp(url, "/test.sdp") == 0) {
        // Serve the SDP file
        const char *sdp =
            "v=0\n"
            "o=- 0 0 IN IP4 127.0.0.1\n"
            "s=Test Stream\n"
            "c=IN IP4 0.0.0.0\n"
            "t=0 0\n"
            "m=video 5000 RTP/AVP 96\n"
            "a=rtpmap:96 H264/90000\n"
            "a=control:stream=0\n";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(sdp), (void *)sdp, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    return MHD_NO;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    // Create a GStreamer pipeline
    pipeline = gst_parse_launch("videotestsrc ! x264enc ! rtph264pay name=pay0 pt=96", NULL);

    // Create an HTTP server for SDP file
    http_daemon = MHD_start_daemon(MHD_USE_AUTO, 8080, NULL, NULL, &http_request_handler, NULL, MHD_OPTION_END);

    // Start the GStreamer pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Run the main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Clean up
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    MHD_stop_daemon(http_daemon);

    return 0;
}