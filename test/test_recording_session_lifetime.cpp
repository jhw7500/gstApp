// Native GLib stress of the exact reference/table functions used by gstApp.
// No cameras, GStreamer pipelines, /tmp markers, or board access.
#include "recordingSessionState.h"
#include "recordingSessionCompletion.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

int main() {
    // Deterministic ownership test. Both readers retain a reference after
    // hash-table remove and the replaced generation must remain independent.
    RecordingSession *first = get_or_create_session("20261029_1730", 0x3u);
    RecordingSession *other = get_or_create_session("20261029_1730", 0x3u);
    assert(first == other);
    assert(g_atomic_int_get(&first->refs) == 3);
    assert(recording_session_forget_if_current("20261029_1730", first));
    assert(g_atomic_int_get(&first->refs) == 2);
    RecordingSession *replacement = get_or_create_session("20261029_1730", 0x1u);
    assert(replacement != first);
    assert(!recording_session_forget_if_current("20261029_1730", first));
    assert(std::strcmp(other->timestamp, "20261029_1730") == 0);
    g_mutex_lock(&first->mutex);
    first->closed_mask = recording_session_mark_closed(
        first->active_mask, first->closed_mask, 0);
    g_mutex_unlock(&first->mutex);
    recording_session_unref(first);
    recording_session_unref(other);
    assert(recording_session_forget_if_current("20261029_1730", replacement));
    recording_session_unref(replacement);

    std::atomic<int> failures(0);
    const char *names[] = {
        "20261029_1740", "20261029_1741", "20261029_1742",
        "20261029_1743", "20261029_1744",
    };
    const int threads = 8;
    const int iterations = 25000;
    std::vector<std::thread> pool;
    for (int thread_index = 0; thread_index < threads; ++thread_index) {
        pool.emplace_back([&, thread_index] {
            for (int i = 0; i < iterations; ++i) {
                const char *key = names[(i + thread_index * 3) % 5];
                RecordingSession *session = get_or_create_session(key, 0x3u);
                if (!session) { ++failures; continue; }
                g_mutex_lock(&session->mutex);
                if (!session->timestamp ||
                    std::strcmp(session->timestamp, key) != 0 ||
                    session->active_mask != 0x3u ||
                    g_atomic_int_get(&session->refs) < 1) {
                    ++failures;
                }
                session->closed_mask = recording_session_mark_closed(
                    session->active_mask, session->closed_mask,
                    static_cast<unsigned>((i + thread_index) % 4));
                g_mutex_unlock(&session->mutex);
                if ((i + thread_index) % 3 == 0)
                    (void)recording_session_forget_if_current(key, session);
                recording_session_unref(session);
                if (i % 127 == 0)
                    std::this_thread::yield();
            }
        });
    }
    for (std::thread &worker : pool)
        worker.join();
    assert(failures.load() == 0);

    g_mutex_lock(&sessions_mutex);
    if (recording_sessions) {
        assert(g_hash_table_size(recording_sessions) <= 5);
        g_hash_table_destroy(recording_sessions);
        recording_sessions = NULL;
    }
    g_mutex_unlock(&sessions_mutex);
    return 0;
}
