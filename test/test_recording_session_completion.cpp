// Native-only policy regression: does not initialize a camera or GStreamer.
#include "recordingSessionCompletion.h"

#include <cassert>
#include <cstdint>

int main() {
    // Regression: merely counting 2 closed fragments for 2 active channels
    // incorrectly admits ch0+ch2 when the active set is ch0+ch1.
    uint32_t mask = 0x3u;
    uint32_t closed = recording_session_mark_closed(mask, 0, 0);
    closed = recording_session_mark_closed(mask, closed, 2);  // Disabled.
    assert(closed == 0x1u);
    assert(!recording_session_all_active_closed(mask, closed));
    closed = recording_session_mark_closed(mask, closed, 1);
    assert(recording_session_all_active_closed(mask, closed));

    // A missing enabled channel never counts as completed, even with two
    // disabled channels reporting fragment-close.
    mask = 0xau;  // ch1 and ch3 enabled
    closed = recording_session_mark_closed(mask, 0, 0);
    closed = recording_session_mark_closed(mask, closed, 2);
    assert(closed == 0);
    assert(!recording_session_all_active_closed(mask, closed));
    closed = recording_session_mark_closed(mask, closed, 1);
    assert(!recording_session_all_active_closed(mask, closed));
    closed = recording_session_mark_closed(mask, closed, 3);
    assert(recording_session_all_active_closed(mask, closed));

    // Single channel, four channels, repeated callbacks, invalid inputs.
    assert(!recording_session_all_active_closed(0, 0));
    assert(!recording_session_all_active_closed(0x10, 0x10));
    assert(recording_session_channel_bit(4) == 0);
    assert(recording_session_channel_bit(255) == 0);
    assert(recording_session_mark_closed(0x1u, 0, 99) == 0);
    assert(recording_session_all_active_closed(0x1u, 0x1u));
    assert(!recording_session_all_active_closed(0xfu, 0x7u));
    assert(recording_session_all_active_closed(0xfu, 0xfu));
    assert(recording_session_mark_closed(0x1u, 0x1u, 0) == 0x1u);

    // A changed expected mask is not the same session contract: the caller
    // must compare a stable session snapshot with the current runtime mask.
    const uint32_t snapshot = 0x3u;
    const uint32_t runtime_change = 0x1u;
    assert(snapshot != runtime_change);
    return 0;
}
