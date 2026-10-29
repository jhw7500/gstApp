// Shared lifetime policy for splitmux recording sessions.
// A hash-table entry owns one ref, and every callback obtains its own ref
// while sessions_mutex is held. Tested under concurrent native GLib threads.
#ifndef GSTAPP_RECORDING_SESSION_STATE_H_
#define GSTAPP_RECORDING_SESSION_STATE_H_

#include <glib.h>

typedef struct _RecordingSession {
    gchar *timestamp;
    guint32 active_mask;
    guint32 closed_mask;
    gboolean ch0_done;
    gboolean ch1_done;
    gboolean ch2_done;
    gboolean ch3_done;
    gboolean srt_done;
    gint refs;  // atomic; hash table owns one, each callback owns one
    GMutex mutex;
} RecordingSession;

// This header is included only in the recorder translation unit and its
// standalone native test. Each process/test translation unit owns one table.
static GHashTable *recording_sessions = NULL;
static GMutex sessions_mutex;

static void recording_session_unref(gpointer data) {
    RecordingSession *session = static_cast<RecordingSession *>(data);
    if (!session || !g_atomic_int_dec_and_test(&session->refs))
        return;
    g_mutex_clear(&session->mutex);
    g_free(session->timestamp);
    g_free(session);
}

// Reference acquisition must be serialized with deletion from the table.
// The callback must call recording_session_unref once on EVERY exit.
static RecordingSession *get_or_create_session(const gchar *timestamp,
                                               guint32 active_mask) {
    g_mutex_lock(&sessions_mutex);
    if (!recording_sessions) {
        recording_sessions = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, recording_session_unref);
    }

    RecordingSession *session = static_cast<RecordingSession *>(
        g_hash_table_lookup(recording_sessions, timestamp));
    if (!session) {
        session = g_new0(RecordingSession, 1);
        session->timestamp = g_strdup(timestamp);
        session->active_mask = active_mask;
        g_atomic_int_set(&session->refs, 1);
        g_mutex_init(&session->mutex);
        g_hash_table_insert(recording_sessions, g_strdup(timestamp), session);
    }
    g_atomic_int_inc(&session->refs);
    g_mutex_unlock(&sessions_mutex);
    return session;
}

// A fragment-closed callback must reference an already-opened session.
// Never infer a channel snapshot from the first CLOSE notification: runtime
// configuration may have changed after the fragment actually opened.
static RecordingSession *recording_session_find(const gchar *timestamp) {
    g_mutex_lock(&sessions_mutex);
    RecordingSession *session = recording_sessions
        ? static_cast<RecordingSession *>(
            g_hash_table_lookup(recording_sessions, timestamp))
        : NULL;
    if (session)
        g_atomic_int_inc(&session->refs);
    g_mutex_unlock(&sessions_mutex);
    return session;
}

// Protect against a callback removing a different generation that was
// inserted under the same timestamp. Caller must still own a reference.
static gboolean recording_session_forget_if_current(
    const gchar *timestamp, const RecordingSession *session) {
    g_mutex_lock(&sessions_mutex);
    const gboolean same = recording_sessions &&
        g_hash_table_lookup(recording_sessions, timestamp) == session;
    if (same)
        g_hash_table_remove(recording_sessions, timestamp);
    g_mutex_unlock(&sessions_mutex);
    return same;
}

#endif  // GSTAPP_RECORDING_SESSION_STATE_H_
