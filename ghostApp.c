#include <gst/gst.h>

static gboolean link_bins_with_ghost_pad(GstBin *bin1, GstBin *bin2) {
    gboolean linked = FALSE;

    // bin1에서 ghost pad 생성
    
    GstPad *ghost_src_pad = gst_element_get_static_pad(GST_ELEMENT(bin1), "src");
    if (!ghost_src_pad) {
        g_printerr("bin1 fail.\n");
        return FALSE;
    }

    // bin2에서 ghost pad 생성
    GstPad *ghost_sink_pad = gst_element_get_static_pad(GST_ELEMENT(bin2), "sink");
    if (!ghost_sink_pad) {
        g_printerr("bin2 fail.\n");
        gst_object_unref(ghost_src_pad);
        return FALSE;
    }

    // ghost pad 끼리 연결
    if (gst_pad_link(ghost_src_pad, ghost_sink_pad) == GST_PAD_LINK_OK) {
        g_print("pad connect.\n");
        linked = TRUE;
    } else {
        g_printerr("pad connect fail.\n");
    }

    gst_object_unref(ghost_src_pad);
    gst_object_unref(ghost_sink_pad);

    return linked;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    // 첫 번째 bin 생성
    GstElement *src = gst_element_factory_make("videotestsrc", "src");
    //GstElement *sink = gst_element_factory_make("autovideosink", "sink");
    GstElement *sink = gst_element_factory_make("queue", "queue");
    if (!src || !sink) {
        g_printerr("not make bin1.\n");
        return -1;
    }
    GstBin *bin1 = gst_bin_new("bin1");
    gst_bin_add_many(bin1, src, sink, NULL);

    // 두 번째 bin 생성
    GstElement *filter = gst_element_factory_make("queue", "queue");
    GstElement *filter_sink = gst_element_factory_make("filesink", "filesink");
    g_object_set(filter_sink, "location", "test.mp4", NULL);
    if (!filter || !filter_sink) {
        g_printerr("not make bin2.\n");
        return -1;
    }
    GstBin *bin2 = gst_bin_new("bin2");
    gst_bin_add_many(bin2, filter, filter_sink, NULL);

    // Ghost pad로 두 bin을 연결
    if (!link_bins_with_ghost_pad(bin1, bin2)) {
        g_printerr("bin1 bin2 not connect.\n");
        gst_object_unref(bin1);
        gst_object_unref(bin2);
        return -1;
    }

    // 파이프라인 생성 및 실행
    GstElement *pipeline = gst_pipeline_new(NULL);
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(bin1));
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(bin2));
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // 메인 루프 실행
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (msg) {
        gst_message_unref(msg);
    }
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // 파이프라인 정지 및 해제
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}