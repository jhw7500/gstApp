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
assert "recording_session_mark_closed(" in source
assert "recording_session_all_active_closed(" in source
assert "session->active_mask != runtime_mask" in source
assert "completed_channels >= active_channels" not in source
assert "recording_session_unref(session);" in source
assert "g_atomic_int_inc(&session->refs)" in source
assert "g_atomic_int_dec_and_test(&session->refs)" in source
assert "g_hash_table_remove(recording_sessions, timestamp)" in source
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
