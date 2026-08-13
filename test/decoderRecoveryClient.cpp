#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>

typedef struct { guint64 samples, idr_count; GstClockTime first_pts, last_pts; gint64 first_ns, last_ns, first_idr_ns; } State;

static gboolean quit_loop(gpointer data)
{
    g_main_loop_quit((GMainLoop *)data);
    return G_SOURCE_REMOVE;
}

static gboolean on_bus(GstBus *bus, GstMessage *message, gpointer data)
{
    (void)bus;
    State *s = (State *)data;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = NULL; gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        g_printerr("DECODER_ERROR message=%s debug=%s\n", error ? error->message : "unknown", debug ? debug : "none");
        if (error)
            g_error_free(error);
        g_free(debug);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        g_printerr("DECODER_EOS\n");
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING) {
        GError *warning = NULL; gchar *debug = NULL;
        gst_message_parse_warning(message, &warning, &debug);
        g_printerr("DECODER_WARNING message=%s debug=%s\n", warning ? warning->message : "unknown", debug ? debug : "none");
        if (warning)
            g_error_free(warning);
        g_free(debug);
    }
    (void)s;
    return G_SOURCE_CONTINUE;
}

static GstFlowReturn on_encoded(GstAppSink *sink, gpointer data)
{
    State *s = (State *)data; GstSample *sample = gst_app_sink_pull_sample(sink); GstBuffer *b = sample ? gst_sample_get_buffer(sample) : NULL; GstMapInfo m;
    if (b && gst_buffer_map(b, &m, GST_MAP_READ)) {
        for (gsize i = 0; i + 4 < m.size; i++) {
            gsize header = 0;
            if (m.data[i] == 0 && m.data[i + 1] == 0 && m.data[i + 2] == 1)
                header = i + 3;
            else if (i + 5 < m.size && m.data[i] == 0 && m.data[i + 1] == 0 &&
                     m.data[i + 2] == 0 && m.data[i + 3] == 1)
                header = i + 4;
            if (header != 0) {
                guint8 t = (m.data[header] >> 1) & 0x3f;
                if (t == 19 || t == 20) {
                    s->idr_count++;
                    if (!s->first_idr_ns) {
                        s->first_idr_ns = g_get_monotonic_time() * 1000;
                        g_print("DECODER_IDR first_ns=%" G_GINT64_FORMAT "\n", s->first_idr_ns);
                    }
                    break;
                }
            }
        }
        gst_buffer_unmap(b, &m);
    }
    if (sample)
        gst_sample_unref(sample);
    return GST_FLOW_OK;
}
static GstFlowReturn on_decoded(GstAppSink *sink, gpointer data)
{
    State *s = (State *)data; GstSample *sample = gst_app_sink_pull_sample(sink); if (!sample) return GST_FLOW_EOS; GstBuffer *b = gst_sample_get_buffer(sample); GstClockTime p = b ? GST_BUFFER_PTS(b) : GST_CLOCK_TIME_NONE; gint64 n = g_get_monotonic_time()*1000;
    if (!s->samples) { s->first_pts=p; s->first_ns=n; g_print("DECODER_FIRST pts=%" G_GUINT64_FORMAT " mono_ns=%" G_GINT64_FORMAT "\n", p, n); } s->last_pts=p; s->last_ns=n; s->samples++; gst_sample_unref(sample); return GST_FLOW_OK;
}
int main(int argc, char **argv)
{
    gst_init(&argc, &argv); const gchar *host=argc>1?argv[1]:"127.0.0.1"; gint seconds=argc>2?atoi(argv[2]):40; GMainLoop *loop=g_main_loop_new(NULL,FALSE); GError *e=NULL;
    gchar *launch=g_strdup_printf("rtspsrc location=rtsp://user:user@%s:8554/ch2 protocols=tcp latency=100 ! rtph265depay ! h265parse ! tee name=t t. ! queue ! appsink name=encoded emit-signals=true sync=false max-buffers=30 drop=true t. ! queue ! vpudec ! appsink name=dec emit-signals=true sync=false max-buffers=30 drop=true",host);
    GstElement *p=gst_parse_launch(launch,&e); g_free(launch); if(!p){g_printerr("DECODER_ERROR %s\n",e?e->message:"unknown"); if(e)g_error_free(e); return 1;}
    State s={0,0,GST_CLOCK_TIME_NONE,GST_CLOCK_TIME_NONE,0,0,0}; GstElement *x=gst_bin_get_by_name(GST_BIN(p),"encoded"); g_signal_connect(x,"new-sample",G_CALLBACK(on_encoded),&s); gst_object_unref(x); x=gst_bin_get_by_name(GST_BIN(p),"dec"); g_signal_connect(x,"new-sample",G_CALLBACK(on_decoded),&s); gst_object_unref(x);
    GstBus *bus = gst_element_get_bus(p);
    guint bus_watch = gst_bus_add_watch(bus, on_bus, &s);
    gst_object_unref(bus);
    gst_element_set_state(p,GST_STATE_PLAYING); g_timeout_add_seconds(seconds, quit_loop, loop); g_main_loop_run(loop); gst_element_set_state(p,GST_STATE_NULL);
    if (bus_watch) g_source_remove(bus_watch);
    g_print("DECODER_SUMMARY samples=%" G_GUINT64_FORMAT " idr_count=%" G_GUINT64_FORMAT " first_idr_ns=%" G_GINT64_FORMAT " first_pts=%" G_GUINT64_FORMAT " last_pts=%" G_GUINT64_FORMAT " elapsed_ns=%" G_GINT64_FORMAT "\n",s.samples,s.idr_count,s.first_idr_ns,s.first_pts,s.last_pts,s.last_ns>s.first_ns?s.last_ns-s.first_ns:0); gst_object_unref(p); g_main_loop_unref(loop); return s.samples?0:2;
}
