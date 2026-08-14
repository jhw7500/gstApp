#include "../encoderStat.h"

#include <glib.h>

static gint failures = 0;
static gint checks = 0;

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        checks++;                                                       \
        if (!(condition))                                               \
        {                                                               \
            g_printerr("FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                       #condition);                                     \
            failures++;                                                 \
        }                                                               \
    } while (0)

typedef struct
{
    EncoderTelemetry *telemetry;
    guint iterations;
} QueueInputThreadData;

static gpointer record_queue_inputs(gpointer user_data)
{
    QueueInputThreadData *data = (QueueInputThreadData *)user_data;

    for (guint i = 0; i < data->iterations; i++)
        data->telemetry->recordQueueInput();

    return NULL;
}

int main(void)
{
    EncoderTelemetry telemetry;
    EncoderTelemetrySnapshot snapshot = telemetry.snapshot(FALSE);

    CHECK(snapshot.q_in == 0);
    CHECK(snapshot.q_out == 0);
    CHECK(snapshot.enc_out == 0);
    CHECK(snapshot.overrun == 0);
    CHECK(snapshot.lvl_buf_max == 0);
    CHECK(snapshot.enc_gap_max_us == 0);

    telemetry.recordQueueInput();
    telemetry.recordQueueOutput();
    telemetry.recordEncoderOutput(1200);
    telemetry.recordOverrun();
    telemetry.recordQueueLevel(4);
    telemetry.recordQueueLevel(2);
    telemetry.recordQueueLevel(7);
    telemetry.recordEncoderOutput(800);

    snapshot = telemetry.snapshot(FALSE);
    CHECK(snapshot.q_in == 1);
    CHECK(snapshot.q_out == 1);
    CHECK(snapshot.enc_out == 2);
    CHECK(snapshot.overrun == 1);
    CHECK(snapshot.lvl_buf_max == 7);
    CHECK(snapshot.enc_gap_max_us == 1200);

    snapshot = telemetry.snapshot(TRUE);
    CHECK(snapshot.q_in == 1);
    CHECK(snapshot.q_out == 1);
    CHECK(snapshot.enc_out == 2);
    CHECK(snapshot.overrun == 1);
    CHECK(snapshot.lvl_buf_max == 7);
    CHECK(snapshot.enc_gap_max_us == 1200);

    snapshot = telemetry.snapshot(FALSE);
    CHECK(snapshot.q_in == 1);
    CHECK(snapshot.q_out == 1);
    CHECK(snapshot.enc_out == 2);
    CHECK(snapshot.overrun == 1);
    CHECK(snapshot.lvl_buf_max == 7);
    CHECK(snapshot.enc_gap_max_us == 0);

    const guint thread_count = 4;
    const guint iterations = 25000;
    GThread *threads[thread_count];
    QueueInputThreadData thread_data = { &telemetry, iterations };

    for (guint i = 0; i < thread_count; i++)
        threads[i] = g_thread_new("encoder-stat-writer",
                                  record_queue_inputs, &thread_data);
    for (guint i = 0; i < thread_count; i++)
        g_thread_join(threads[i]);

    snapshot = telemetry.snapshot(FALSE);
    CHECK(snapshot.q_in == 100001);
    CHECK(snapshot.q_out == 1);
    CHECK(snapshot.enc_out == 2);
    CHECK(snapshot.overrun == 1);
    CHECK(snapshot.lvl_buf_max == 7);
    CHECK(snapshot.enc_gap_max_us == 0);

    g_print("encoder telemetry test: %d checks, %d failures -> %s\n",
            checks, failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
