#!/usr/bin/env bash
# Offline, board-free regression of gstApp session completion.
set -euo pipefail

cd "$(dirname "$0")/.."
command -v g++ >/dev/null
command -v python3 >/dev/null

# Assert the actual production callback uses the tested helper. A standalone
# helper passing in isolation is not proof of wired recording behavior.
python3 - <<'PY'
from pathlib import Path

source = Path("muxSinkBin.cpp").read_text()
header = Path("recordingSessionCompletion.h").read_text()

assert '#include "recordingSessionCompletion.h"' in source
assert '#include "recordingSessionState.h"' in source
assert "recording_session_mark_closed(" in source
assert "recording_session_all_active_closed(" in source
assert "session->active_mask != runtime_mask" in source
assert "completed_channels >= active_channels" not in source
assert "recording_session_unref(session);" in source
state = Path("recordingSessionState.h").read_text()
assert "g_atomic_int_inc(&session->refs)" in state
assert "g_atomic_int_dec_and_test(&session->refs)" in state
assert "g_hash_table_remove(recording_sessions, timestamp)" in state
assert "recording_session_forget_if_current(timestamp, session)" in source
assert "g_mutex_lock(&sessions_mutex)" in state
assert "static inline bool recording_session_all_active_closed(" in header
print("gstApp source integration policy PASS")
PY

temp_dir="$(mktemp -d -t gst-session-mask-XXXXXX)"
trap 'rm -rf -- "$temp_dir"' EXIT

g++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  -I. test/test_recording_session_completion.cpp \
  -o "$temp_dir/recording-session-completion"
"$temp_dir/recording-session-completion"

echo "gstApp native channel-mask regression PASS"

# Stress the ACTUAL shared GLib hash lookup/remove/ref/unref implementation,
# not a reconstructed model. Never touches real recording files or services.
command -v pkg-config >/dev/null
pkg-config --exists glib-2.0
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -pedantic -pthread \
  -I. $(pkg-config --cflags glib-2.0) \
  test/test_recording_session_lifetime.cpp \
  -o "$temp_dir/recording-session-lifetime" \
  $(pkg-config --libs glib-2.0)
"$temp_dir/recording-session-lifetime"
echo "gstApp concurrent session lookup/remove lifetime stress PASS"
