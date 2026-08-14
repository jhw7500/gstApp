#include "encoderStat.h"

EncoderTelemetry::EncoderTelemetry()
    : q_in_(0),
      q_out_(0),
      enc_out_(0),
      overrun_(0),
      lvl_buf_max_(0),
      enc_gap_max_us_(0)
{
}

void EncoderTelemetry::recordQueueInput()
{
    q_in_.fetch_add(1, std::memory_order_relaxed);
}

void EncoderTelemetry::recordQueueOutput()
{
    q_out_.fetch_add(1, std::memory_order_relaxed);
}

void EncoderTelemetry::recordEncoderOutput(gint64 gap_us)
{
    enc_out_.fetch_add(1, std::memory_order_relaxed);

    gint64 maximum = enc_gap_max_us_.load(std::memory_order_relaxed);
    while (gap_us > maximum &&
           !enc_gap_max_us_.compare_exchange_weak(
               maximum, gap_us, std::memory_order_relaxed))
    {
    }
}

void EncoderTelemetry::recordOverrun()
{
    overrun_.fetch_add(1, std::memory_order_relaxed);
}

void EncoderTelemetry::recordQueueLevel(guint level)
{
    guint maximum = lvl_buf_max_.load(std::memory_order_relaxed);
    while (level > maximum &&
           !lvl_buf_max_.compare_exchange_weak(
               maximum, level, std::memory_order_relaxed))
    {
    }
}

EncoderTelemetrySnapshot EncoderTelemetry::snapshot(gboolean reset_gap)
{
    EncoderTelemetrySnapshot result;
    result.q_in = q_in_.load(std::memory_order_relaxed);
    result.q_out = q_out_.load(std::memory_order_relaxed);
    result.enc_out = enc_out_.load(std::memory_order_relaxed);
    result.overrun = overrun_.load(std::memory_order_relaxed);
    result.lvl_buf_max = lvl_buf_max_.load(std::memory_order_relaxed);
    if (reset_gap)
        result.enc_gap_max_us = enc_gap_max_us_.exchange(
            0, std::memory_order_relaxed);
    else
        result.enc_gap_max_us = enc_gap_max_us_.load(
            std::memory_order_relaxed);

    return result;
}
