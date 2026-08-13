#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if grep -nE 'GSTAPP_(V4L2_SYNC_TRACE_SEC|CHANNEL_SYNC_TRACE_SEC|RTSP_SYNC_TRACE_SEC|RTSP_FRAME_ID_SEI|RTSP_TEST_STALL)' \
    videoBin.cpp encoderBin.cpp rtspServerBin.cpp; then
  echo "legacy sync environment control remains" >&2
  exit 1
fi
grep -q 'cmdArg.v4l2_sync_trace_sec' videoBin.cpp
grep -q 'cmdArg.channel_sync_trace_sec' encoderBin.cpp
grep -q 'cmdArg.rtsp_sync_trace_sec' rtspServerBin.cpp
grep -q 'cmdArg.rtsp_frame_id_sei' rtspServerBin.cpp
grep -q 'cmdArg.rtsp_test_stall_ch' rtspServerBin.cpp
python3 - <<'PY'
from pathlib import Path
s = Path('rtspServerBin.cpp').read_text(encoding='utf-8')
needle = 'gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_out_queue")'
pos = s.index(needle)
if 'if (info->sync_trace)' not in s[max(0, pos - 500):pos]:
    raise SystemExit('rtsp_out_queue lookup is not guarded by sync_trace')
print('sync config source contract: PASSED')
PY
