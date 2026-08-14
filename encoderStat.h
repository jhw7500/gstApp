#ifndef ENCODER_STAT_H
#define ENCODER_STAT_H

#include <glib.h>

#include <atomic>

typedef struct
{
    guint64 q_in;
    guint64 q_out;
    guint64 enc_out;
    guint64 overrun;
    guint lvl_buf_max;
    gint64 enc_gap_max_us;
} EncoderTelemetrySnapshot;

class EncoderTelemetry
{
public:
    EncoderTelemetry();

    void recordQueueInput();
    void recordQueueOutput();
    void recordEncoderOutput(gint64 gap_us);
    void recordOverrun();
    void recordQueueLevel(guint level);
    EncoderTelemetrySnapshot snapshot(gboolean reset_gap);

private:
    std::atomic<guint64> q_in_;
    std::atomic<guint64> q_out_;
    std::atomic<guint64> enc_out_;
    std::atomic<guint64> overrun_;
    std::atomic<guint> lvl_buf_max_;
    std::atomic<gint64> enc_gap_max_us_;
};

#endif
