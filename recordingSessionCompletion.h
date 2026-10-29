// Standalone channel completion policy for gstApp's splitmux recorder.
// Kept free of GStreamer/GLib dependencies for deterministic native regression.
#ifndef GSTAPP_RECORDING_SESSION_COMPLETION_H_
#define GSTAPP_RECORDING_SESSION_COMPLETION_H_

#include <stdint.h>

enum { GSTAPP_RECORDING_CHANNELS = 4 };
enum { GSTAPP_RECORDING_VALID_MASK = 0x0fU };

static inline uint32_t recording_session_channel_bit(unsigned channel) {
    return channel < GSTAPP_RECORDING_CHANNELS ? (1u << channel) : 0u;
}

static inline uint32_t recording_session_mark_closed(
    uint32_t active_mask, uint32_t closed_mask, unsigned channel) {
    const uint32_t bit = recording_session_channel_bit(channel);
    if (!bit || !(active_mask & bit))
        return closed_mask;  // Ignore disabled or invalid channel callbacks.
    return closed_mask | bit;
}

static inline bool recording_session_all_active_closed(
    uint32_t active_mask, uint32_t closed_mask) {
    return active_mask != 0
        && (active_mask & ~static_cast<uint32_t>(GSTAPP_RECORDING_VALID_MASK)) == 0
        && (closed_mask & active_mask) == active_mask;
}

#endif  // GSTAPP_RECORDING_SESSION_COMPLETION_H_
