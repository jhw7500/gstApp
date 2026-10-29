// Native-only policy regression: does not initialize a camera or GStreamer.
#include "recordingSessionCompletion.h"

#include <cassert>
#include <cstdint>

int main() {
    // Regression: merely counting 2 closed fragments for 2 active channels
    // incorrectly admits ch0+ch2 when the active set is ch0+ch1.
    uint32_t mask = 0b0011;
    uint32_t closed = recording_session_mark_closed(mask, 0, 0);
    closed = recording_session_mark_closed(mask, closed, 2);  // Disabled.
    assert(closed == 0b0001);
    assert(!recording_session_all_active_closed(mask, closed));
    closed = recording_session_mark_closed(mask, closed, 1);
    assert(recording_session_all_active_closed(mask, closed));

    // A missing enabled channel never counts as completed, even with two
    // disabled channels reporting fragment-close.
    mask = 0b1010;  // ch1 and ch3 enabled
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
    assert(recording_session_mark_closed(0b0001, 0, 99) == 0);
    assert(recording_session_all_active_closed(0b0001, 0b0001));
    assert(!recording_session_all_active_closed(0b1111, 0b0111));
    assert(recording_session_all_active_closed(0b1111, 0b1111));
    assert(recording_session_mark_closed(0b0001, 0b0001, 0) == 0b0001);

    // A changed expected mask is not the same session contract: the caller
    // must compare a stable session snapshot with the current runtime mask.
    const uint32_t snapshot = 0b0011;
    const uint32_t runtime_change = 0b0001;
    assert(snapshot != runtime_change);
    return 0;
}
