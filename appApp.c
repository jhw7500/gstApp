#include <gst/gst.h>
#include <stdio.h>
//gchar file_name[128];
GMainLoop *loop;

typedef struct _CustomData{
    gchar* file_name;
    guint8 ch;
} CustomData;

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

static gboolean change_file_name(gpointer data) {
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");

    //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
    //memcpy(file_name, tmp, 128);
    info->file_name = g_strdup_printf("output_%s-ch%d.mp4", date_str, info->ch);
    g_date_time_unref(datetime);
    g_free(date_str);
    //g_free(tmp);
    //g_print("file_name:%s\n", info->file_name);
    //g_free(file_name);

    return TRUE;
}
// appsink의 "new-sample" 시그널 처리 함수
static GstFlowReturn new_sample_handler(GstElement *appsink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)data;

    // sample을 가져오기
    g_signal_emit_by_name(appsink, "pull-sample", &sample);
    if (!sample) {
        g_print("4.\n");
        return GST_FLOW_ERROR;
    }

    // 버퍼 가져오기
    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        g_print("5.\n");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 파일로 저장
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    //g_print("file_name:%s\n", info->file_name);
    FILE *file = fopen(info->file_name, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        g_printerr("6.\n");
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

static GstFlowReturn new_preroll_handler(GstElement *appsink, gpointer data) {
    // Preroll frame 처리 작업 추가 가능
    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    g_print("Preroll frame\n");
    sink_name = gst_object_get_name(GST_OBJECT(appsink));
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
        return GST_FLOW_ERROR;
    }
    change_file_name(info);

    return GST_FLOW_OK;
}

GstElement *pipeline;

void sigintHandler(int unused) {
	g_print("You ctrl-c-ed!");
	gst_element_send_event(pipeline, gst_event_new_eos()); 
	return;
}

void sigkillHandler(int unused) {
	g_print("You killed!");
	gst_element_send_event(pipeline, gst_event_new_eos()); 
	return;
}

void sigtermHandler(int unused) {
	g_print("You terminated!");
	gst_element_send_event(pipeline, gst_event_new_eos()); 
	return;
}

void attachInterruptHandlers()
{
    signal(SIGINT, sigintHandler);
    signal(SIGKILL, sigkillHandler);
    signal(SIGTERM, sigtermHandler);
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    attachInterruptHandlers();
#if 0
    MainPipe pipe;
    CustomData info[2];

    pipe.pipeline = gst_pipeline_new("pipeline");
    pipe.src = gst_element_factory_make("v4l2src", "src");
    pipe.capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    pipe.tee = gst_element_factory_make("tee", "mainTee");
    pipe.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");

    // 캡처 포맷 설정 (video3 장치의 적절한 캡처 포맷에 맞게 변경)
    g_object_set(pipe.src, "device", "/dev/video4", NULL);
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "width", G_TYPE_INT, 3840,
                                        "height", G_TYPE_INT, 1080,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
    g_object_set(pipe.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if(!pipe.pipeline || !pipe.src || !pipe.capsfilter || !pipe.tee || !pipe.convert)
    {
        g_printerr("Main pipe make error\n");
        return -1;
    }

    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.src, pipe.capsfilter, pipe.tee, pipe.convert, NULL);
    
    if (!gst_element_link_many(pipe.src, pipe.capsfilter, pipe.tee, NULL)) {
        g_printerr("main link err.\n");
        gst_object_unref(pipe.pipeline);
        return -1;
    }

    pipe.cam[0].tee = gst_element_factory_make("tee", "camTee_left");
    pipe.cam[0].crop = gst_element_factory_make("videocrop", "crop_left");
    pipe.cam[0].encoder[0] = gst_element_factory_make("vpuenc_h264", "encoder0");
    pipe.cam[0].queue[0] = gst_element_factory_make("queue", "queue0");
    pipe.cam[0].sink[0] = gst_element_factory_make("appsink", "sink0");

    g_object_set(pipe.cam[0].crop, "left", 1920, NULL);
    g_object_set(pipe.cam[0].encoder[0], "bitrate", 4096, NULL);
    g_object_set(pipe.cam[0].sink[0], "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(pipe.cam[0].sink[0], "new-sample", G_CALLBACK(new_sample_handler), &info);
    g_signal_connect(pipe.cam[0].sink[0], "new-preroll", G_CALLBACK(new_preroll_handler), &info);

    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.cam[0].crop, pipe.cam[0].tee, pipe.cam[0].encoder[0], pipe.cam[0].queue[0], pipe.cam[0].sink[0], NULL);

    if (!gst_element_link_many(pipe.tee, pipe.cam[0].crop, pipe.cam[0].encoder[0], pipe.cam[0].queue[0], pipe.cam[0].tee, pipe.cam[0].sink[0], NULL)) {
        g_printerr("main link err.\n");
        gst_object_unref(pipe.pipeline);
        return -1;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipe.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("3.\n");
        gst_object_unref(pipe.pipeline);
        return -1;
    }
    g_timeout_add_seconds(60, (GSourceFunc)change_file_name, &info);
    //g_timeout_add(100, (GSourceFunc)change_file_name, &info[0]);
    //change_file_name(&info);
    //g_print("file_name:%s\n", info.file_name);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    // 재생 시간 기다리기 (10초)
    //gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    // 파이프라인 정지 및 해제
    gst_element_set_state(pipe.pipeline, GST_STATE_NULL);
    gst_object_unref(pipe.pipeline);
#if 0
    pipe.cam[0].encoder[1] = gst_element_factory_make("vpuenc_h264", "encoder1");
    pipe.cam[0].queue[1] = gst_element_factory_make("queue", "queue1");
    pipe.cam[0].sink[1] = gst_element_factory_make("appsink", "sink1");
    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.cam[0].encoder[1], pipe.cam[0].queue[1], pipe.cam[0].sink[1], NULL);

    pipe.cam[1].crop = gst_element_factory_make("videocrop", "crop_right");
    pipe.cam[1].tee = gst_element_factory_make("tee", "camTee_right");
    pipe.cam[1].encoder[0] = gst_element_factory_make("vpuenc_h264", "encoder0");
    pipe.cam[1].queue[0] = gst_element_factory_make("queue", "queue0");
    pipe.cam[1].sink[0] = gst_element_factory_make("appsink", "sink0");
    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.cam[1].crop, pipe.cam[1].tee, pipe.cam[1].encoder[1], pipe.cam[1].queue[0], pipe.cam[1].sink[0], NULL);

    pipe.cam[1].encoder[1] = gst_element_factory_make("vpuenc_h264", "encoder1");
    pipe.cam[1].queue[1] = gst_element_factory_make("queue", "queue1");
    pipe.cam[1].sink[1] = gst_element_factory_make("appsink", "sink1");
    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.cam[1].encoder[1], pipe.cam[1].queue[1], pipe.cam[1].sink[1], NULL);
#endif

#else
    CustomData info[2];
    //g_allocator_new
    // 파이프라인 생성
    pipeline = gst_pipeline_new("pipeline");

    // 요소 생성
    GstElement *src = gst_element_factory_make("v4l2src", "src");
    GstElement *convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "caps");
    GstElement *encoder0 = gst_element_factory_make("vpuenc_h264", "encoder0");
    GstElement *encoder1 = gst_element_factory_make("vpuenc_h264", "encoder1");
    GstElement *queue0 = gst_element_factory_make("queue", "queue0");
    GstElement *queue1 = gst_element_factory_make("queue", "queue1");
    GstElement *appsink0 = gst_element_factory_make("appsink", "sink0");
    GstElement *appsink1 = gst_element_factory_make("appsink", "sink1");
    GstElement *tee = gst_element_factory_make("tee", "tee");
    GstElement *crop_left = gst_element_factory_make("videocrop", "crop_left");
    GstElement *crop_right = gst_element_factory_make("videocrop", "crop_right");
    GstElement *tee1 = gst_element_factory_make("tee", "tee1");
    GstElement *tee2 = gst_element_factory_make("tee", "tee2");
    GstElement *parse0 = gst_element_factory_make("h264parse", "parse0");
    GstElement *parse1 = gst_element_factory_make("h264parse", "parse1");
    // 요소가 생성되지 않은 경우 에러 처리
    if (!pipeline || !convert || !src || !capsfilter || !encoder0 || !encoder1 || !queue0 || !queue1 || !appsink0 || !appsink1 || !tee 
    || !crop_left || !crop_right || !tee1 || !tee2 || !parse0 || !parse1) 
    {
        g_printerr("1.\n");
        return -1;
    }

    // 파이프라인에 요소 추가
    gst_bin_add_many(GST_BIN(pipeline), src, convert, capsfilter, encoder0, encoder1, queue0, queue1, appsink0, appsink1, tee, crop_left, crop_right, 
                    parse0, parse1, tee1, tee2, NULL);

    // 요소 연결
    if (!gst_element_link_many(src, capsfilter, convert, tee, NULL)) {
        g_printerr("main link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    if (!gst_element_link_many(tee, crop_left, queue0, encoder0, parse0, tee1, NULL)) {
        g_printerr("left link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    if (!gst_element_link_many(tee, crop_right, queue1, encoder1, parse1, tee2, NULL)) {
        g_printerr("right link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    if (!gst_element_link_many(tee1, appsink0, NULL)) {
        g_printerr("left file link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    if (!gst_element_link_many(tee2, appsink1, NULL)) {
        g_printerr("right file link err.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    g_object_set(crop_left, "left", 1920, NULL);
    g_object_set(crop_left, "right", 0, NULL);
	g_object_set(crop_left, "top", 0, NULL);
	g_object_set(crop_left, "bottom", 0, NULL);
    g_object_set(crop_right, "left", 0, NULL);
    g_object_set(crop_right, "right", 1920, NULL);
	g_object_set(crop_right, "top", 0, NULL);
	g_object_set(crop_right, "bottom", 0, NULL);
    
    g_object_set(src, "device", "/dev/video3", NULL);
    g_object_set(src, "do-timestamp", TRUE, NULL);
    g_object_set(encoder0, "bitrate", 4096, NULL);
    g_object_set(encoder0, "gop-size", 30, NULL);
    g_object_set(encoder1, "bitrate", 1024, NULL);
    g_object_set(encoder1, "gop-size", 30, NULL);

    g_object_set(queue0, "max-size-buffers", 60, NULL);
    g_object_set(queue0, "max-size-bytes", 0, NULL);
    g_object_set(queue0, "max-size-time", 0, NULL);
    g_object_set(queue0, "leaky", 1, NULL);
    // appsink의 "new-sample" 시그널에 핸들러 등록
    //gchar *file_name = "output.mp4"; // 저장할 파일 이름 (수정 가능)
	g_object_set(appsink0, "max-buffers", 60, NULL);
	g_object_set(appsink0, "drop", TRUE, NULL);
    g_object_set(appsink0, "emit-signals", TRUE, "sync", FALSE, NULL);
	g_object_set(appsink1, "max-buffers", 60, NULL);
	g_object_set(appsink1, "drop", TRUE, NULL);
    g_object_set(appsink1, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(appsink0, "new-sample", G_CALLBACK(new_sample_handler), &info[0]);
    g_signal_connect(appsink0, "new-preroll", G_CALLBACK(new_preroll_handler), &info[0]);
    //g_signal_connect(appsink1, "new-sample", G_CALLBACK(new_sample_handler), &info[1]);
    //g_signal_connect(appsink1, "new-preroll", G_CALLBACK(new_preroll_handler), &info[1]);
    // 캡처 포맷 설정 (video3 장치의 적절한 캡처 포맷에 맞게 변경)
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, 3840,
                                        "height", G_TYPE_INT, 1080,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    // 파이프라인 실행
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("3.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    g_timeout_add_seconds(60, (GSourceFunc)change_file_name, &info[0]);
    //g_timeout_add_seconds(60, (GSourceFunc)change_file_name, &info[1]);
    //g_timeout_add(100, (GSourceFunc)change_file_name, &info[0]);
    //change_file_name(&info);
    //g_print("file_name:%s\n", info.file_name);

    GstBus *bus = gst_element_get_bus(pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    // 재생 시간 기다리기 (10초)
    //gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    // 파이프라인 정지 및 해제
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
#endif
    return 0;
}