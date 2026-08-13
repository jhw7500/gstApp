/*
 * RTSP 4-channel frame ID synchronization validation client.
 *
 * H.265 registered user-data SEI에 포함된 GSTSYNC1 frame ID를 읽어 네 채널의
 * 최초 공통 ID부터 frame group을 생성한다.
 */

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <signal.h>
#include <string.h>

#define SYNC_CHANNELS 4
#define FRAME_ID_MAGIC_SIZE 8
#define FRAME_ID_PAYLOAD_REST_SIZE 20
#define DEFAULT_TEST_SECONDS 60
#define DEFAULT_QUEUE_LIMIT 300
#define SYNC_EPOCHS 8

static const guint8 frame_id_magic[FRAME_ID_MAGIC_SIZE] = {
    'G', 'S', 'T', 'S', 'Y', 'N', 'C', '1'
};

typedef struct
{
    guint64 id;
    guint64 origin_pts;
    GstClockTime receive_time;
    GstClockTime client_pts;
} SyncFrame;

typedef struct _ClientData ClientData;

typedef struct
{
    ClientData *client;
    guint ch;
} ChannelData;

struct _ClientData
{
    GMainLoop *loop;
    GstElement *pipeline[SYNC_CHANNELS];
    guint bus_watch_id[SYNC_CHANNELS];
    gchar *host;
    gchar *user;
    gchar *password;
    gint port;
    GMutex lock;
    GQueue queue[SYNC_CHANNELS];
    ChannelData channel[SYNC_CHANNELS];
    gboolean barrier_ready;
    gboolean waiting_reconnect;
    gboolean reconnect_started;
    gboolean reconnect_completed;
    gboolean expect_reconnect_first[SYNC_CHANNELS];
    guint epoch;
    guint barrier_count;
    gint reconnect_channel;
    gint reconnect_after;
    gint reconnect_gap;
    gint reconnect_count;
    gint reconnect_interval;
    guint reconnect_attempts;
    guint reconnect_successes;
    gint stall_channel;
    gint stall_after;
    gint stall_duration;
    gint stall_delay_ms;
    gboolean stall_active;
    gboolean stall_started;
    gboolean stall_completed;
    gboolean stall_release_seen;
    gboolean stall_recovered;
    guint64 stall_groups_at_start;
    guint64 stall_groups_at_stop;
    guint64 stall_last_group_at_start;
    guint64 stall_last_group_at_stop;
    guint64 stall_release_first_id;
    guint64 stall_recovery_id;
    guint64 stall_recovery_lag_ids;
    gint64 stall_recovery_elapsed_ms;
    guint64 barrier_id;
    guint64 epoch_barrier_id[SYNC_EPOCHS];
    guint64 epoch_groups[SYNC_EPOCHS];
    guint64 epoch_first_group_id[SYNC_EPOCHS];
    guint64 epoch_last_group_id[SYNC_EPOCHS];
    guint64 epoch_initial_drops[SYNC_EPOCHS][SYNC_CHANNELS];
    guint64 epoch_skipped_ids[SYNC_EPOCHS];
    guint64 reconnect_skipped_ids[SYNC_CHANNELS];
    guint64 group_mismatches;
    guint64 groups;
    guint64 first_group_id;
    guint64 last_group_id;
    guint64 channel_frames[SYNC_CHANNELS];
    guint64 channel_first_id[SYNC_CHANNELS];
    guint64 channel_last_id[SYNC_CHANNELS];
    guint64 channel_gaps[SYNC_CHANNELS];
    guint64 channel_gap_events[SYNC_CHANNELS];
    guint64 channel_last_gap_first_id[SYNC_CHANNELS];
    guint64 channel_last_gap_last_id[SYNC_CHANNELS];
    guint64 channel_groups_after_gap[SYNC_CHANNELS];
    guint64 channel_duplicates[SYNC_CHANNELS];
    guint64 initial_drops[SYNC_CHANNELS];
    guint64 alignment_drops[SYNC_CHANNELS];
    guint64 invalid_sei[SYNC_CHANNELS];
    GstClockTime receive_spread_min;
    GstClockTime receive_spread_max;
    guint64 receive_spread_sum;
    GstClockTime pts_spread_min;
    GstClockTime pts_spread_max;
    guint64 pts_spread_sum;
    const gchar *stop_reason;
    gint64 start_time_us;
    gint64 stop_time_us;
};

static GMainLoop *signal_loop = NULL;
static ClientData *signal_client = NULL;

static GstFlowReturn new_sample_handler(GstAppSink *sink,
                                        gpointer user_data);
static gboolean bus_handler(GstBus *bus, GstMessage *message,
                            gpointer user_data);
static gboolean disconnect_handler(gpointer user_data);

static gboolean stall_start_handler(gpointer user_data)
{
    ClientData *client = (ClientData *)user_data;

    g_mutex_lock(&client->lock);
    client->stall_active = TRUE;
    client->stall_started = TRUE;
    client->stall_groups_at_start = client->groups;
    client->stall_last_group_at_start = client->last_group_id;
    g_mutex_unlock(&client->lock);

    g_print("FRAME_SYNC_STALL_START ch=%d elapsed_ms=%" G_GINT64_FORMAT
            " duration_seconds=%d delay_ms=%d\n",
            client->stall_channel,
            (g_get_monotonic_time() - client->start_time_us) / 1000,
            client->stall_duration, client->stall_delay_ms);
    return G_SOURCE_REMOVE;
}

static gboolean stall_stop_handler(gpointer user_data)
{
    ClientData *client = (ClientData *)user_data;

    g_mutex_lock(&client->lock);
    client->stall_active = FALSE;
    client->stall_completed = TRUE;
    client->stall_groups_at_stop = client->groups;
    client->stall_last_group_at_stop = client->last_group_id;
    g_mutex_unlock(&client->lock);

    g_print("FRAME_SYNC_STALL_STOP ch=%d elapsed_ms=%" G_GINT64_FORMAT
            " groups_during=%" G_GUINT64_FORMAT "\n",
            client->stall_channel,
            (g_get_monotonic_time() - client->start_time_us) / 1000,
            client->stall_groups_at_stop - client->stall_groups_at_start);
    return G_SOURCE_REMOVE;
}

static void signal_handler(gint signum)
{
    (void)signum;
    if (signal_client)
    {
        signal_client->stop_reason = "signal";
        signal_client->stop_time_us = g_get_monotonic_time();
    }
    if (signal_loop)
        g_main_loop_quit(signal_loop);
}

static gboolean read_frame_id(const guint8 *data, gsize size,
                              guint8 *version, guint8 *channel,
                              guint64 *frame_id, guint64 *origin_pts)
{
    if (!data || size < FRAME_ID_MAGIC_SIZE + FRAME_ID_PAYLOAD_REST_SIZE)
        return FALSE;

    for (gsize offset = 0;
         offset + FRAME_ID_MAGIC_SIZE + FRAME_ID_PAYLOAD_REST_SIZE <= size;
         offset++)
    {
        if (memcmp(data + offset, frame_id_magic, FRAME_ID_MAGIC_SIZE) != 0)
            continue;

        guint8 payload[FRAME_ID_PAYLOAD_REST_SIZE] = { 0 };
        guint payload_size = 0;
        guint zero_count = 0;
        gsize pos = offset + FRAME_ID_MAGIC_SIZE;

        while (pos < size && payload_size < sizeof(payload))
        {
            guint8 value = data[pos++];
            if (zero_count >= 2 && value == 0x03)
            {
                zero_count = 0;
                continue;
            }

            payload[payload_size++] = value;
            zero_count = value == 0 ? zero_count + 1 : 0;
        }

        if (payload_size != sizeof(payload))
            return FALSE;

        *version = payload[0];
        *channel = payload[1];
        *frame_id = GST_READ_UINT64_BE(payload + 4);
        *origin_pts = GST_READ_UINT64_BE(payload + 12);
        return TRUE;
    }

    return FALSE;
}

static void update_spread(GstClockTime spread, GstClockTime *minimum,
                          GstClockTime *maximum, guint64 *sum)
{
    if (*minimum == GST_CLOCK_TIME_NONE || spread < *minimum)
        *minimum = spread;
    if (spread > *maximum)
        *maximum = spread;
    *sum += spread;
}

static void clear_queues(ClientData *client)
{
    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        while (!g_queue_is_empty(&client->queue[ch]))
            g_free(g_queue_pop_head(&client->queue[ch]));
    }
}

static void destroy_channel_pipeline(ClientData *client, guint ch)
{
    if (client->bus_watch_id[ch] != 0)
    {
        g_source_remove(client->bus_watch_id[ch]);
        client->bus_watch_id[ch] = 0;
    }

    if (client->pipeline[ch])
    {
        gst_element_set_state(client->pipeline[ch], GST_STATE_NULL);
        gst_object_unref(client->pipeline[ch]);
        client->pipeline[ch] = NULL;
    }
}

static gboolean create_channel_pipeline(ClientData *client, guint ch,
                                        GError **error)
{
    gchar *uri = g_strdup_printf("rtsp://%s:%d/ch%u",
                                 client->host, client->port, ch);
    gchar *escaped_uri = g_strescape(uri, NULL);
    gchar *escaped_user = g_strescape(client->user, NULL);
    gchar *escaped_password = g_strescape(client->password, NULL);
    gchar *launch = g_strdup_printf(
        "rtspsrc location=\"%s\" user-id=\"%s\" user-pw=\"%s\" "
        "protocols=tcp latency=100 ! rtph265depay ! "
        "h265parse disable-passthrough=true config-interval=-1 "
        "! video/x-h265,stream-format=byte-stream,alignment=au "
        "! appsink name=sink%u emit-signals=true sync=false "
        "max-buffers=120 drop=true",
        escaped_uri, escaped_user, escaped_password, ch);

    g_free(escaped_password);
    g_free(escaped_user);
    g_free(escaped_uri);
    g_free(uri);

    client->pipeline[ch] = gst_parse_launch(launch, error);
    g_free(launch);
    if (!client->pipeline[ch])
        return FALSE;

    gchar *sink_name = g_strdup_printf("sink%u", ch);
    GstElement *sink =
        gst_bin_get_by_name(GST_BIN(client->pipeline[ch]), sink_name);
    g_free(sink_name);
    if (!sink)
    {
        destroy_channel_pipeline(client, ch);
        return FALSE;
    }

    g_signal_connect(sink, "new-sample", G_CALLBACK(new_sample_handler),
                     &client->channel[ch]);
    gst_object_unref(sink);

    GstBus *bus = gst_element_get_bus(client->pipeline[ch]);
    client->bus_watch_id[ch] =
        gst_bus_add_watch(bus, bus_handler, &client->channel[ch]);
    gst_object_unref(bus);
    return client->bus_watch_id[ch] != 0;
}

static gboolean align_front_frames(ClientData *client, gboolean initial)
{
    while (TRUE)
    {
        guint64 target = 0;

        for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
        {
            if (g_queue_is_empty(&client->queue[ch]))
                return FALSE;
            SyncFrame *frame = (SyncFrame *)g_queue_peek_head(&client->queue[ch]);
            if (frame->id > target)
                target = frame->id;
        }

        gboolean dropped = FALSE;
        for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
        {
            while (!g_queue_is_empty(&client->queue[ch]))
            {
                SyncFrame *frame =
                    (SyncFrame *)g_queue_peek_head(&client->queue[ch]);
                if (frame->id >= target)
                    break;

                g_free(g_queue_pop_head(&client->queue[ch]));
                if (initial)
                {
                    client->initial_drops[ch]++;
                    client->epoch_initial_drops[client->epoch][ch]++;
                }
                else
                    client->alignment_drops[ch]++;
                dropped = TRUE;
            }

            if (g_queue_is_empty(&client->queue[ch]))
                return FALSE;
        }

        if (!dropped)
            return TRUE;
    }
}

static void process_groups(ClientData *client)
{
    if (client->waiting_reconnect)
        return;

    if (!client->barrier_ready)
    {
        if (!align_front_frames(client, TRUE))
            return;

        SyncFrame *frame = (SyncFrame *)g_queue_peek_head(&client->queue[0]);
        client->barrier_ready = TRUE;
        client->barrier_id = frame->id;
        client->epoch_barrier_id[client->epoch] = frame->id;
        client->barrier_count++;
        g_print("FRAME_SYNC_BARRIER epoch=%u id=%" G_GUINT64_FORMAT
                " initial_drop=%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
                ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "\n",
                client->epoch, client->barrier_id,
                client->epoch_initial_drops[client->epoch][0],
                client->epoch_initial_drops[client->epoch][1],
                client->epoch_initial_drops[client->epoch][2],
                client->epoch_initial_drops[client->epoch][3]);
    }

    while (align_front_frames(client, FALSE))
    {
        SyncFrame *frame[SYNC_CHANNELS] = { NULL };
        guint64 id = 0;
        guint64 origin_pts = 0;
        GstClockTime receive_min = GST_CLOCK_TIME_NONE;
        GstClockTime receive_max = 0;
        GstClockTime pts_min = GST_CLOCK_TIME_NONE;
        GstClockTime pts_max = 0;
        gboolean valid = TRUE;

        for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
        {
            frame[ch] = (SyncFrame *)g_queue_pop_head(&client->queue[ch]);
            if (ch == 0)
            {
                id = frame[ch]->id;
                origin_pts = frame[ch]->origin_pts;
            }
            else if (frame[ch]->id != id || frame[ch]->origin_pts != origin_pts)
            {
                valid = FALSE;
            }

            if (receive_min == GST_CLOCK_TIME_NONE ||
                frame[ch]->receive_time < receive_min)
                receive_min = frame[ch]->receive_time;
            if (frame[ch]->receive_time > receive_max)
                receive_max = frame[ch]->receive_time;

            if (GST_CLOCK_TIME_IS_VALID(frame[ch]->client_pts))
            {
                if (pts_min == GST_CLOCK_TIME_NONE ||
                    frame[ch]->client_pts < pts_min)
                    pts_min = frame[ch]->client_pts;
                if (frame[ch]->client_pts > pts_max)
                    pts_max = frame[ch]->client_pts;
            }
        }

        if (valid)
        {
            if (client->groups == 0)
                client->first_group_id = id;
            client->last_group_id = id;
            client->groups++;
            if (client->epoch_groups[client->epoch] == 0)
                client->epoch_first_group_id[client->epoch] = id;
            client->epoch_last_group_id[client->epoch] = id;
            client->epoch_groups[client->epoch]++;

            for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
            {
                if (client->channel_gap_events[ch] > 0 &&
                    id > client->channel_last_gap_last_id[ch])
                {
                    client->channel_groups_after_gap[ch]++;
                }
            }

            if (client->stall_completed && !client->stall_recovered)
            {
                guint64 latest_id = id;
                for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
                {
                    if (client->channel_last_id[ch] > latest_id)
                        latest_id = client->channel_last_id[ch];
                }

                guint64 lag_ids = latest_id - id;
                if (lag_ids <= 1)
                {
                    client->stall_recovered = TRUE;
                    client->stall_recovery_id = id;
                    client->stall_recovery_lag_ids = lag_ids;
                    client->stall_recovery_elapsed_ms =
                        (g_get_monotonic_time() - client->start_time_us) / 1000;
                    g_print("FRAME_SYNC_STALL_RECOVERED ch=%d id=%"
                            G_GUINT64_FORMAT " elapsed_ms=%" G_GINT64_FORMAT
                            " lag_ids=%" G_GUINT64_FORMAT "\n",
                            client->stall_channel, id,
                            client->stall_recovery_elapsed_ms, lag_ids);
                }
            }

            update_spread(receive_max - receive_min,
                          &client->receive_spread_min,
                          &client->receive_spread_max,
                          &client->receive_spread_sum);
            if (pts_min != GST_CLOCK_TIME_NONE)
            {
                update_spread(pts_max - pts_min, &client->pts_spread_min,
                              &client->pts_spread_max,
                              &client->pts_spread_sum);
            }
        }
        else
        {
            client->group_mismatches++;
        }

        for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
            g_free(frame[ch]);
    }
}

static gboolean reconnect_handler(gpointer user_data)
{
    ClientData *client = (ClientData *)user_data;
    guint ch = (guint)client->reconnect_channel;

    GError *error = NULL;
    gboolean created = create_channel_pipeline(client, ch, &error);
    GstStateChangeReturn result = created
        ? gst_element_set_state(client->pipeline[ch], GST_STATE_PLAYING)
        : GST_STATE_CHANGE_FAILURE;
    g_print("FRAME_SYNC_RECONNECT_UP ch=%u elapsed_ms=%" G_GINT64_FORMAT
            " attempt=%u state_result=%d error=%s\n",
            ch, (g_get_monotonic_time() - client->start_time_us) / 1000,
            client->reconnect_attempts, result,
            error ? error->message : "none");
    if (error)
        g_error_free(error);

    if (result == GST_STATE_CHANGE_FAILURE)
    {
        client->stop_reason = "reconnect-failed";
        client->stop_time_us = g_get_monotonic_time();
        g_main_loop_quit(client->loop);
    }
    else
    {
        g_mutex_lock(&client->lock);
        client->waiting_reconnect = FALSE;
        client->reconnect_successes++;
        client->reconnect_completed =
            client->reconnect_successes == (guint)client->reconnect_count;
        g_mutex_unlock(&client->lock);

        if (client->reconnect_successes < (guint)client->reconnect_count)
        {
            g_timeout_add_seconds((guint)client->reconnect_interval,
                                  disconnect_handler, client);
        }
    }

    return G_SOURCE_REMOVE;
}

static gboolean disconnect_handler(gpointer user_data)
{
    ClientData *client = (ClientData *)user_data;
    guint ch = (guint)client->reconnect_channel;

    g_mutex_lock(&client->lock);
    if (client->reconnect_attempts >= (guint)client->reconnect_count)
    {
        g_mutex_unlock(&client->lock);
        return G_SOURCE_REMOVE;
    }
    client->reconnect_attempts++;
    client->epoch = client->reconnect_attempts;
    client->waiting_reconnect = TRUE;
    client->reconnect_started = TRUE;
    client->barrier_ready = FALSE;
    clear_queues(client);
    g_mutex_unlock(&client->lock);

    destroy_channel_pipeline(client, ch);

    g_mutex_lock(&client->lock);
    clear_queues(client);
    client->expect_reconnect_first[ch] = TRUE;
    g_mutex_unlock(&client->lock);

    g_print("FRAME_SYNC_RECONNECT_DOWN ch=%u elapsed_ms=%" G_GINT64_FORMAT
            " attempt=%u gap_seconds=%d\n",
            ch, (g_get_monotonic_time() - client->start_time_us) / 1000,
            client->reconnect_attempts, client->reconnect_gap);

    g_timeout_add_seconds((guint)client->reconnect_gap,
                          reconnect_handler, client);
    return G_SOURCE_REMOVE;
}

static GstFlowReturn new_sample_handler(GstAppSink *sink, gpointer user_data)
{
    ChannelData *channel = (ChannelData *)user_data;
    ClientData *client = channel->client;
    gboolean apply_stall = FALSE;

    g_mutex_lock(&client->lock);
    apply_stall = client->stall_active &&
                  client->stall_channel == (gint)channel->ch;
    g_mutex_unlock(&client->lock);
    if (apply_stall)
        g_usleep((gulong)client->stall_delay_ms * 1000);

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map = GST_MAP_INFO_INIT;
    guint8 version = 0;
    guint8 payload_channel = 0;
    guint64 frame_id = 0;
    guint64 origin_pts = 0;
    gboolean magic_present = FALSE;
    gboolean valid = buffer && gst_buffer_map(buffer, &map, GST_MAP_READ) &&
                     read_frame_id(map.data, map.size, &version,
                                   &payload_channel, &frame_id, &origin_pts);

    if (buffer && map.data) {
        for (gsize i = 0; i + FRAME_ID_MAGIC_SIZE <= map.size; i++) {
            if (memcmp(map.data + i, frame_id_magic, FRAME_ID_MAGIC_SIZE) == 0) {
                magic_present = TRUE;
                break;
            }
        }
    }

    if (buffer && map.data)
        gst_buffer_unmap(buffer, &map);

    g_mutex_lock(&client->lock);
    if (!valid || version != 1 || payload_channel != channel->ch)
    {
        client->invalid_sei[channel->ch]++;
        if (client->invalid_sei[channel->ch] <= 2)
            g_print("FRAME_SYNC_INVALID ch=%u size=%" G_GSIZE_FORMAT
                    " magic=%d version=%u payload_ch=%u\n",
                    channel->ch, map.size, magic_present, version,
                    payload_channel);
        g_mutex_unlock(&client->lock);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    guint ch = channel->ch;
    if (client->stall_completed && !client->stall_release_seen &&
        client->stall_channel == (gint)ch)
    {
        client->stall_release_seen = TRUE;
        client->stall_release_first_id = frame_id;
        g_print("FRAME_SYNC_STALL_FIRST ch=%u id=%" G_GUINT64_FORMAT
                " elapsed_ms=%" G_GINT64_FORMAT "\n",
                ch, frame_id,
                (g_get_monotonic_time() - client->start_time_us) / 1000);
    }
    if (client->channel_frames[ch] == 0)
    {
        client->channel_first_id[ch] = frame_id;
    }
    else if (client->expect_reconnect_first[ch])
    {
        guint64 skipped = 0;
        if (frame_id > client->channel_last_id[ch] + 1)
        {
            skipped = frame_id - client->channel_last_id[ch] - 1;
            client->reconnect_skipped_ids[ch] += skipped;
        }
        client->epoch_skipped_ids[client->epoch] = skipped;
        client->expect_reconnect_first[ch] = FALSE;
        g_print("FRAME_SYNC_RECONNECT_FIRST ch=%u epoch=%u id=%"
                G_GUINT64_FORMAT " skipped=%" G_GUINT64_FORMAT "\n",
                ch, client->epoch, frame_id, skipped);
    }
    else if (frame_id > client->channel_last_id[ch] + 1)
    {
        client->channel_gap_events[ch]++;
        client->channel_last_gap_first_id[ch] =
            client->channel_last_id[ch] + 1;
        client->channel_last_gap_last_id[ch] = frame_id - 1;
        client->channel_groups_after_gap[ch] = 0;
        client->channel_gaps[ch] +=
            frame_id - client->channel_last_id[ch] - 1;
        g_print("FRAME_SYNC_GAP ch=%u event=%" G_GUINT64_FORMAT
                " first_missing=%" G_GUINT64_FORMAT " last_missing=%"
                G_GUINT64_FORMAT " next_id=%" G_GUINT64_FORMAT "\n",
                ch, client->channel_gap_events[ch],
                client->channel_last_gap_first_id[ch],
                client->channel_last_gap_last_id[ch], frame_id);
    }
    else if (frame_id <= client->channel_last_id[ch])
    {
        client->channel_duplicates[ch]++;
    }

    client->channel_frames[ch]++;
    client->channel_last_id[ch] = frame_id;

    SyncFrame *frame = g_new0(SyncFrame, 1);
    frame->id = frame_id;
    frame->origin_pts = origin_pts;
    frame->receive_time = (GstClockTime)g_get_monotonic_time() * GST_USECOND;
    frame->client_pts = GST_BUFFER_PTS(buffer);
    g_queue_push_tail(&client->queue[ch], frame);

    while (g_queue_get_length(&client->queue[ch]) > DEFAULT_QUEUE_LIMIT)
    {
        g_free(g_queue_pop_head(&client->queue[ch]));
        client->alignment_drops[ch]++;
    }

    process_groups(client);
    g_mutex_unlock(&client->lock);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static gboolean bus_handler(GstBus *bus, GstMessage *message,
                            gpointer user_data)
{
    (void)bus;
    ChannelData *channel = (ChannelData *)user_data;
    ClientData *client = channel->client;

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        g_printerr("FRAME_SYNC_ERROR ch=%u source=%s error=%s debug=%s\n",
                   channel->ch, GST_OBJECT_NAME(message->src),
                   error ? error->message : "unknown",
                   debug ? debug : "none");
        if (error)
            g_error_free(error);
        g_free(debug);
        client->stop_reason = "error";
        client->stop_time_us = g_get_monotonic_time();
        g_main_loop_quit(client->loop);
        break;
    }
    case GST_MESSAGE_EOS:
        g_print("FRAME_SYNC_EOS ch=%u source=%s\n",
                channel->ch, GST_OBJECT_NAME(message->src));
        client->stop_reason = "eos";
        client->stop_time_us = g_get_monotonic_time();
        g_main_loop_quit(client->loop);
        break;
    default:
        break;
    }

    return TRUE;
}

static gboolean timeout_handler(gpointer user_data)
{
    ClientData *client = (ClientData *)user_data;
    client->stop_reason = "timeout";
    client->stop_time_us = g_get_monotonic_time();
    g_main_loop_quit(client->loop);
    return G_SOURCE_REMOVE;
}

static void print_summary(ClientData *client)
{
    g_mutex_lock(&client->lock);
    guint64 receive_avg = client->groups > 0
                             ? client->receive_spread_sum / client->groups
                             : 0;
    guint64 pts_avg = client->groups > 0
                         ? client->pts_spread_sum / client->groups
                         : 0;
    gint64 elapsed_ms = client->stop_time_us > client->start_time_us
                            ? (client->stop_time_us - client->start_time_us) / 1000
                            : 0;

    g_print("FRAME_SYNC_SUMMARY stop_reason=%s elapsed_ms=%" G_GINT64_FORMAT
            " barrier_ready=%d barrier_id=%"
            G_GUINT64_FORMAT " groups=%" G_GUINT64_FORMAT
            " barrier_count=%u reconnect_started=%d reconnect_completed=%d"
            " reconnect_attempts=%u reconnect_successes=%u"
            " stall_started=%d stall_completed=%d stall_recovered=%d"
            " group_mismatches=%" G_GUINT64_FORMAT
            " first_group=%" G_GUINT64_FORMAT " last_group=%"
            G_GUINT64_FORMAT " receive_spread_min_ns=%" G_GUINT64_FORMAT
            " receive_spread_avg_ns=%" G_GUINT64_FORMAT
            " receive_spread_max_ns=%" G_GUINT64_FORMAT
            " pts_spread_min_ns=%" G_GUINT64_FORMAT
            " pts_spread_avg_ns=%" G_GUINT64_FORMAT
            " pts_spread_max_ns=%" G_GUINT64_FORMAT "\n",
            client->stop_reason, elapsed_ms, client->barrier_ready,
            client->barrier_id, client->groups,
            client->barrier_count, client->reconnect_started,
            client->reconnect_completed, client->reconnect_attempts,
            client->reconnect_successes, client->stall_started,
            client->stall_completed, client->stall_recovered,
            client->group_mismatches,
            client->first_group_id, client->last_group_id,
            client->receive_spread_min == GST_CLOCK_TIME_NONE
                ? 0 : client->receive_spread_min,
            receive_avg, client->receive_spread_max,
            client->pts_spread_min == GST_CLOCK_TIME_NONE
                ? 0 : client->pts_spread_min,
            pts_avg, client->pts_spread_max);

    if (client->stall_channel >= 0)
    {
        g_print("FRAME_SYNC_STALL_SUMMARY ch=%d groups_at_start=%"
                G_GUINT64_FORMAT " last_group_at_start=%" G_GUINT64_FORMAT
                " groups_at_stop=%" G_GUINT64_FORMAT
                " last_group_at_stop=%" G_GUINT64_FORMAT
                " release_first_id=%" G_GUINT64_FORMAT
                " recovery_id=%" G_GUINT64_FORMAT
                " recovery_elapsed_ms=%" G_GINT64_FORMAT
                " recovery_lag_ids=%" G_GUINT64_FORMAT "\n",
                client->stall_channel, client->stall_groups_at_start,
                client->stall_last_group_at_start,
                client->stall_groups_at_stop,
                client->stall_last_group_at_stop,
                client->stall_release_first_id,
                client->stall_recovery_id,
                client->stall_recovery_elapsed_ms,
                client->stall_recovery_lag_ids);
    }

    guint epoch_count = client->reconnect_channel >= 0
                            ? (guint)client->reconnect_count + 1
                            : 1;
    for (guint epoch = 0; epoch < epoch_count; epoch++)
    {
        g_print("FRAME_SYNC_EPOCH epoch=%u barrier_id=%" G_GUINT64_FORMAT
                " groups=%" G_GUINT64_FORMAT " first_group=%"
                G_GUINT64_FORMAT " last_group=%" G_GUINT64_FORMAT
                " initial_drop=%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
                ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
                " skipped_ids=%" G_GUINT64_FORMAT "\n",
                epoch, client->epoch_barrier_id[epoch],
                client->epoch_groups[epoch],
                client->epoch_first_group_id[epoch],
                client->epoch_last_group_id[epoch],
                client->epoch_initial_drops[epoch][0],
                client->epoch_initial_drops[epoch][1],
                client->epoch_initial_drops[epoch][2],
                client->epoch_initial_drops[epoch][3],
                client->epoch_skipped_ids[epoch]);
    }

    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        g_print("FRAME_SYNC_CHANNEL ch=%u frames=%" G_GUINT64_FORMAT
                " first_id=%" G_GUINT64_FORMAT " last_id=%"
                G_GUINT64_FORMAT " gaps=%" G_GUINT64_FORMAT
                " gap_events=%" G_GUINT64_FORMAT
                " last_gap_first=%" G_GUINT64_FORMAT
                " last_gap_last=%" G_GUINT64_FORMAT
                " groups_after_gap=%" G_GUINT64_FORMAT
                " duplicates=%" G_GUINT64_FORMAT " initial_drop=%"
                G_GUINT64_FORMAT " alignment_drop=%" G_GUINT64_FORMAT
                " invalid_sei=%" G_GUINT64_FORMAT " queued=%u\n",
                ch, client->channel_frames[ch],
                client->channel_first_id[ch], client->channel_last_id[ch],
                client->channel_gaps[ch], client->channel_gap_events[ch],
                client->channel_last_gap_first_id[ch],
                client->channel_last_gap_last_id[ch],
                client->channel_groups_after_gap[ch],
                client->channel_duplicates[ch],
                client->initial_drops[ch], client->alignment_drops[ch],
                client->invalid_sei[ch],
                g_queue_get_length(&client->queue[ch]));
        if (client->reconnect_skipped_ids[ch] > 0)
        {
            g_print("FRAME_SYNC_RECONNECT_CHANNEL ch=%u skipped_ids=%"
                    G_GUINT64_FORMAT "\n",
                    ch, client->reconnect_skipped_ids[ch]);
        }
    }
    g_mutex_unlock(&client->lock);
}

int main(int argc, char *argv[])
{
    gchar *host = g_strdup("127.0.0.1");
    gchar *user = g_strdup("user");
    gchar *password = g_strdup("user");
    gint port = 8554;
    gint seconds = DEFAULT_TEST_SECONDS;
    gint reconnect_channel = -1;
    gint reconnect_after = 0;
    gint reconnect_gap = 0;
    gint reconnect_count = 0;
    gint reconnect_interval = 0;
    gint stall_channel = -1;
    gint stall_after = 0;
    gint stall_duration = 0;
    gint stall_delay_ms = 0;
    GOptionEntry options[] = {
        { "host", 0, 0, G_OPTION_ARG_STRING, &host, "RTSP server host", NULL },
        { "port", 0, 0, G_OPTION_ARG_INT, &port, "RTSP server port", NULL },
        { "user", 0, 0, G_OPTION_ARG_STRING, &user, "RTSP user", NULL },
        { "password", 0, 0, G_OPTION_ARG_STRING, &password,
          "RTSP password", NULL },
        { "seconds", 0, 0, G_OPTION_ARG_INT, &seconds,
          "Measurement seconds", NULL },
        { "reconnect-channel", 0, 0, G_OPTION_ARG_INT,
          &reconnect_channel, "Channel to disconnect and reconnect", NULL },
        { "reconnect-after", 0, 0, G_OPTION_ARG_INT,
          &reconnect_after, "Seconds before intentional disconnect", NULL },
        { "reconnect-gap", 0, 0, G_OPTION_ARG_INT,
          &reconnect_gap, "Intentional disconnect duration in seconds", NULL },
        { "reconnect-count", 0, 0, G_OPTION_ARG_INT,
          &reconnect_count, "Number of reconnect cycles", NULL },
        { "reconnect-interval", 0, 0, G_OPTION_ARG_INT,
          &reconnect_interval, "Seconds from reconnect to next disconnect",
          NULL },
        { "stall-channel", 0, 0, G_OPTION_ARG_INT,
          &stall_channel, "Channel to apply client-side backpressure", NULL },
        { "stall-after", 0, 0, G_OPTION_ARG_INT,
          &stall_after, "Seconds before client-side stall", NULL },
        { "stall-duration", 0, 0, G_OPTION_ARG_INT,
          &stall_duration, "Client-side stall duration in seconds", NULL },
        { "stall-delay-ms", 0, 0, G_OPTION_ARG_INT,
          &stall_delay_ms, "Delay per sample during stall", NULL },
        { NULL }
    };

    GOptionContext *context =
        g_option_context_new("- 4-channel RTSP frame ID sync client");
    g_option_context_add_main_entries(context, options, NULL);
    g_option_context_add_group(context, gst_init_get_option_group());
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_printerr("option parse failed: %s\n", error->message);
        g_error_free(error);
        g_option_context_free(context);
        return 1;
    }
    g_option_context_free(context);

    if (port <= 0 || port > 65535 || seconds <= 0)
    {
        g_printerr("invalid port or seconds\n");
        return 1;
    }
    if (reconnect_channel >= 0 && reconnect_count == 0)
        reconnect_count = 1;
    if (reconnect_channel >= 0 &&
        (reconnect_channel >= SYNC_CHANNELS || reconnect_after <= 0 ||
         reconnect_gap <= 0 || reconnect_count <= 0 ||
         reconnect_count >= SYNC_EPOCHS ||
         (reconnect_count > 1 && reconnect_interval <= 0) ||
         reconnect_after + reconnect_count * reconnect_gap +
             (reconnect_count - 1) * reconnect_interval >= seconds))
    {
        g_printerr("invalid reconnect options\n");
        return 1;
    }
    if (reconnect_channel < 0 &&
        (reconnect_after != 0 || reconnect_gap != 0 ||
         reconnect_count != 0 || reconnect_interval != 0))
    {
        g_printerr("reconnect channel is required\n");
        return 1;
    }
    if (stall_channel >= 0 &&
        (stall_channel >= SYNC_CHANNELS || stall_after <= 0 ||
         stall_duration <= 0 || stall_delay_ms <= 0 ||
         stall_after + stall_duration >= seconds))
    {
        g_printerr("invalid stall options\n");
        return 1;
    }
    if (stall_channel < 0 &&
        (stall_after != 0 || stall_duration != 0 || stall_delay_ms != 0))
    {
        g_printerr("stall channel is required\n");
        return 1;
    }
    if (stall_channel >= 0 && reconnect_channel >= 0)
    {
        g_printerr("stall and reconnect modes cannot be combined\n");
        return 1;
    }

    ClientData client = {};
    client.loop = g_main_loop_new(NULL, FALSE);
    client.receive_spread_min = GST_CLOCK_TIME_NONE;
    client.pts_spread_min = GST_CLOCK_TIME_NONE;
    client.stop_reason = "unknown";
    client.reconnect_channel = reconnect_channel;
    client.reconnect_after = reconnect_after;
    client.reconnect_gap = reconnect_gap;
    client.reconnect_count = reconnect_count;
    client.reconnect_interval = reconnect_interval;
    client.stall_channel = stall_channel;
    client.stall_after = stall_after;
    client.stall_duration = stall_duration;
    client.stall_delay_ms = stall_delay_ms;
    client.host = host;
    client.user = user;
    client.password = password;
    client.port = port;
    g_mutex_init(&client.lock);
    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        g_queue_init(&client.queue[ch]);
        client.channel[ch].client = &client;
        client.channel[ch].ch = ch;
    }

    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        if (!create_channel_pipeline(&client, ch, &error))
        {
            g_printerr("pipeline ch%u create failed: %s\n", ch,
                       error ? error->message : "unknown");
            if (error)
                g_error_free(error);
            for (guint cleanup = 0; cleanup < SYNC_CHANNELS; cleanup++)
                destroy_channel_pipeline(&client, cleanup);
            return 1;
        }
    }
    g_timeout_add_seconds((guint)seconds, timeout_handler, &client);
    if (client.reconnect_channel >= 0)
    {
        g_timeout_add_seconds((guint)client.reconnect_after,
                              disconnect_handler, &client);
    }
    if (client.stall_channel >= 0)
    {
        g_timeout_add_seconds((guint)client.stall_after,
                              stall_start_handler, &client);
        g_timeout_add_seconds(
            (guint)(client.stall_after + client.stall_duration),
            stall_stop_handler, &client);
    }

    signal_loop = client.loop;
    signal_client = &client;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        if (gst_element_set_state(client.pipeline[ch], GST_STATE_PLAYING) ==
            GST_STATE_CHANGE_FAILURE)
        {
            g_printerr("pipeline ch%u state change failed\n", ch);
            for (guint cleanup = 0; cleanup < SYNC_CHANNELS; cleanup++)
                destroy_channel_pipeline(&client, cleanup);
            return 1;
        }
    }

    g_print("FRAME_SYNC_START host=%s port=%d seconds=%d"
            " reconnect_channel=%d reconnect_after=%d reconnect_gap=%d"
            " reconnect_count=%d reconnect_interval=%d"
            " stall_channel=%d stall_after=%d stall_duration=%d"
            " stall_delay_ms=%d\n",
            host, port, seconds, client.reconnect_channel,
            client.reconnect_after, client.reconnect_gap,
            client.reconnect_count, client.reconnect_interval,
            client.stall_channel, client.stall_after,
            client.stall_duration, client.stall_delay_ms);
    client.start_time_us = g_get_monotonic_time();
    g_main_loop_run(client.loop);
    if (client.stop_time_us == 0)
        client.stop_time_us = g_get_monotonic_time();
    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
    {
        if (client.pipeline[ch])
            gst_element_set_state(client.pipeline[ch], GST_STATE_NULL);
    }
    print_summary(&client);

    clear_queues(&client);
    for (guint ch = 0; ch < SYNC_CHANNELS; ch++)
        destroy_channel_pipeline(&client, ch);
    g_main_loop_unref(client.loop);
    g_mutex_clear(&client.lock);
    signal_client = NULL;
    signal_loop = NULL;
    g_free(password);
    g_free(user);
    g_free(host);

    gboolean success = client.barrier_ready && client.groups > 0 &&
                       client.group_mismatches == 0;
    if (client.reconnect_channel >= 0)
    {
        success = success && client.reconnect_started &&
                  client.reconnect_completed &&
                  client.reconnect_attempts == (guint)client.reconnect_count &&
                  client.reconnect_successes == (guint)client.reconnect_count &&
                  client.barrier_count == (guint)client.reconnect_count + 1;
        for (guint epoch = 1;
             epoch <= (guint)client.reconnect_count; epoch++)
        {
            success = success && client.epoch_groups[epoch] > 0;
        }
    }
    if (client.stall_channel >= 0)
    {
        success = success && client.stall_started &&
                  client.stall_completed && client.stall_release_seen &&
                  client.stall_recovered;
    }
    return success ? 0 : 2;
}
