#include <gst/gst.h>
#include <glib.h>
#include <signal.h>

// 글로벌 변수로 파이프라인을 선언
GstElement *pipeline;

void handle_sigint(int sig) {
    g_print("Caught signal %d, sending EOS to pipeline\n", sig);
    gst_element_send_event(pipeline, gst_event_new_eos());
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_printerr("Error: %s\n", error->message);
            g_free(debug);
            g_error_free(error);

            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    GstElement *v4l2src, *queue1, *videorate, *vpuenc_h264, *h264parse;
    GstElement *alsasrc, *audioconvert, *audioresample, *capsfilter, *lamemp3enc, *mpegaudioparse, *queue2;
    GstElement *mp4mux, *filesink;
    GstPad *audio_pad, *video_pad;
    GstBus *bus;
    GMainLoop *loop;
    GstStateChangeReturn ret;

    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    // 파이프라인 요소 생성
    pipeline = gst_pipeline_new("video-audio-pipeline");

    // 비디오 요소 생성 및 설정
    v4l2src = gst_element_factory_make("v4l2src", "v4l2src");
    queue1 = gst_element_factory_make("queue", "queue1");
    videorate = gst_element_factory_make("videorate", "videorate");
    vpuenc_h264 = gst_element_factory_make("vpuenc_h264", "vpuenc_h264");
    h264parse = gst_element_factory_make("h264parse", "h264parse");

    g_object_set(G_OBJECT(v4l2src), "device", "/dev/video4", NULL);
    g_object_set(G_OBJECT(vpuenc_h264), "bitrate", 2048, NULL);

    GstCaps *video_caps = gst_caps_new_simple("video/x-raw",
                                              "format", G_TYPE_STRING, "NV12",
                                              "width", G_TYPE_INT, 1920,
                                              "height", G_TYPE_INT, 1080,
                                              "framerate", GST_TYPE_FRACTION, 15, 1,
                                              NULL);

    GstCaps *videorate_caps = gst_caps_new_simple("video/x-raw",
                                                  "framerate", GST_TYPE_FRACTION, 15, 1,
                                                  NULL);

    // 오디오 요소 생성 및 설정
    alsasrc = gst_element_factory_make("alsasrc", "alsasrc");
    audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
    audioresample = gst_element_factory_make("audioresample", "audioresample");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    lamemp3enc = gst_element_factory_make("lamemp3enc", "lamemp3enc");
    mpegaudioparse = gst_element_factory_make("mpegaudioparse", "mpegaudioparse");
    queue2 = gst_element_factory_make("queue", "queue2");

    g_object_set(G_OBJECT(alsasrc), "device", "plughw:0,0", NULL);
    g_object_set(G_OBJECT(lamemp3enc), "target", 1, "bitrate", 192, NULL);

    GstCaps *audio_caps = gst_caps_new_simple("audio/x-raw",
                                              "format", G_TYPE_STRING, "S32LE",
                                              "rate", G_TYPE_INT, 48000,
                                              "channels", G_TYPE_INT, 1,
                                              NULL);

    GstCaps *audioresample_caps = gst_caps_new_simple("audio/x-raw",
                                                      "channels", G_TYPE_INT, 1,
                                                      NULL);

    g_object_set(G_OBJECT(capsfilter), "caps", audioresample_caps, NULL);

    // Mux 및 싱크 요소 생성 및 설정
    mp4mux = gst_element_factory_make("mp4mux", "mp4mux");
    filesink = gst_element_factory_make("filesink", "filesink");
    g_object_set(G_OBJECT(filesink), "location", "test1.mp4", NULL);

    // 요소들이 모두 생성되었는지 확인
    if (!pipeline || !v4l2src || !queue1 || !videorate || !vpuenc_h264 || !h264parse ||
        !alsasrc || !audioconvert || !audioresample || !capsfilter || !lamemp3enc || !mpegaudioparse || !queue2 ||
        !mp4mux || !filesink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    // 파이프라인에 요소 추가
    gst_bin_add_many(GST_BIN(pipeline), v4l2src, queue1, videorate, vpuenc_h264, h264parse, alsasrc,
                     audioconvert, audioresample, capsfilter, lamemp3enc, mpegaudioparse, queue2, mp4mux, filesink, NULL);

    // 비디오 요소 링크
    if (!gst_element_link_filtered(v4l2src, queue1, video_caps) ||
        !gst_element_link_many(queue1, videorate, vpuenc_h264, h264parse, NULL)) {
        g_printerr("Video elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // 오디오 요소 링크
    if (!gst_element_link_filtered(alsasrc, audioconvert, audio_caps) ||
        !gst_element_link_many(audioconvert, audioresample, capsfilter, lamemp3enc, mpegaudioparse, queue2, NULL)) {
        g_printerr("Audio elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

#if 0
    // Mux 요소의 비디오 및 오디오 패드 가져오기
    video_pad = gst_element_get_request_pad(mp4mux, "video_0");
    audio_pad = gst_element_get_request_pad(mp4mux, "audio_0");
    if (!video_pad || !audio_pad) {
        g_printerr("Failed to get request pads from mp4mux\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // h264parse의 src 패드와 mp4mux의 비디오 패드를 링크
    GstPad *h264parse_src_pad = gst_element_get_static_pad(h264parse, "src");
    if (gst_pad_link(h264parse_src_pad, video_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link h264parse to mp4mux video pad\n");
        gst_object_unref(pipeline);
        return -1;
    }
    gst_object_unref(h264parse_src_pad);

    // queue2의 src 패드와 mp4mux의 오디오 패드를 링크
    GstPad *queue2_src_pad = gst_element_get_static_pad(queue2, "src");
    if (gst_pad_link(queue2_src_pad, audio_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link queue2 to mp4mux audio pad\n");
        gst_object_unref(pipeline);
        return -1;
    }
    gst_object_unref(queue2_src_pad);

    // mp4mux와 filesink 링크
    if (!gst_element_link(mp4mux, filesink)) {
        g_printerr("Mux elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }
#else
    gst_element_link_many(h264parse, mp4mux, filesink, NULL);
    gst_element_link_many(queue2, mp4mux, filesink, NULL);
#endif

    gst_caps_unref(video_caps);
    gst_caps_unref(audio_caps);
    gst_caps_unref(videorate_caps);
    gst_caps_unref(audioresample_caps);

    // 파이프라인 버스에서 메시지 처리
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // SIGINT 시그널 핸들러 설정
    signal(SIGINT, handle_sigint);

    // 파이프라인 실행
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // 메인 루프 실행
    g_main_loop_run(loop);

    // 파이프라인 상태 NULL로 변경 후 리소스 해제
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);

    return 0;
}
