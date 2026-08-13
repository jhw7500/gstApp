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
sync = Path('rtspSync.cpp').read_text(encoding='utf-8')
frame = Path('rtspFrameId.h').read_text(encoding='utf-8')
main = Path('main.cpp').read_text(encoding='utf-8')
audio = Path('audioBin.cpp').read_text(encoding='utf-8')

required = {
    'generation-owned media state': 'g_object_set_data_full(G_OBJECT(media), "gstapp-rtsp-sync-generation"',
    'matching generation detach': 'info->sync_generation == generation',
    'generation cancellation': 'rtsp_sync_generation_deactivate(generation)',
    'cancellable stall': 'rtsp_sync_generation_wait(',
    'one-shot stall removal': 'return GST_PAD_PROBE_REMOVE;',
    'audio/channel caller guard': 'rtsp_frame_id_sei_enabled() && info->ch < MAX_CHANNEL',
    'defensive AU helper guard': '!rtsp_h265_annex_b_au_buffer(buffer)',
    'strict encoded caps': '"stream-format", G_TYPE_STRING, "byte-stream",\n      "alignment", G_TYPE_STRING, "au"',
    'dual parser before caps': 're.enc, re.parse,\n                                    re.capsfilter2, re.sink',
    'single parser retained': 're.queue, re.parse, re.capsfilter2,\n                                  re.sink',
    'standard meta duration': 'GST_CLOCK_TIME_NONE) != NULL',
    'generation summaries': 'generation=%" G_GUINT64_FORMAT',
    'RTSP attach source retained': 'rtspServerSource_id = gst_rtsp_server_attach',
    'RTSP attach source removed': 'g_source_remove(rtspServerSource_id)',
    'RTSP mount reference released': 'g_object_unref(rtspMounts)',
    'RTSP clients synchronously removed': 'gst_rtsp_server_client_filter(',
}
for label, needle in required.items():
    if needle not in s:
        raise SystemExit(f'missing source contract: {label}')

if 'info->sync_trace' in s:
    raise SystemExit('legacy per-bin trace state remains')
if 'origin_index' in s:
    raise SystemExit('non-time trace ordinal remains in timestamp metadata path')
if 'g_usleep((gulong)ctx->duration_sec' in s:
    raise SystemExit('full-duration stall sleep remains')

audio_decl = main.index('AudioBin audioBin;')
video_loop = main.index('for (i = 0; i < MAX_CHANNEL; i++)', audio_decl)
pre_video = main[audio_decl:video_loop]
if pre_video.count('rtspServerStart()') != 1 or \
        'cmdArg.stream_en[STREAM_RTSP]' not in pre_video:
    raise SystemExit('RTSP server is not initialized once before all mounts')
if main[video_loop:].count('rtspServerStart()') != 0:
    raise SystemExit('RTSP server initialization remains video-channel scoped')
shared_audio = main.index('/* AudioBin is shared by every recording mux',
                          video_loop)
post_video_loop = main.index('// Enable timestamp debug at startup',
                             shared_audio)
if 'audioBin.init()' in main[video_loop:shared_audio]:
    raise SystemExit('shared audio bin is still initialized per video channel')
if main[shared_audio:post_video_loop].count('audioBin.init()') != 1:
    raise SystemExit('shared audio bin is not initialized once after video setup')
shared_audio_block = main[shared_audio:post_video_loop]
audio_rtsp_guard = shared_audio_block.find(
    'if (cmdArg.stream_en[STREAM_RTSP])')
audio_rtsp_init = shared_audio_block.find('rtspServerBin[4].audioInit()')
if audio_rtsp_guard < 0 or audio_rtsp_init < audio_rtsp_guard:
    raise SystemExit('audio RTSP mount is not gated by STREAM_RTSP')
if 'be.enc, be.parse, be.queue2, be.tee' not in ' '.join(audio.split()):
    raise SystemExit('audio parser is not linked into the shared encoded path')
if 'return -1;' in audio[audio.index('gboolean AudioBin::init()'):]:
    raise SystemExit('AudioBin::init failure is truthy instead of FALSE')
if 'cmdArg.levelMode == MODE_TEST ? "audiotestsrc" : "alsasrc"' not in \
        ' '.join(audio.split()):
    raise SystemExit('test mode does not provide deterministic audio input')
constructor = s[s.index('RtspServerBin::RtspServerBin()'):
                s.index('RtspServerBin::~RtspServerBin()')]
if 'rtsp_sync_trace_new' in constructor:
    raise SystemExit('trace still allocated at bin construction')

configure_publish = s[s.index('info->appsrc = appsrc;'):
                      s.index('if (old_generation)',
                              s.index('info->appsrc = appsrc;'))]
if 'info->wait_keyframe = info->ch < MAX_CHANNEL;' not in configure_publish or \
        'info->wait_keyframe = FALSE;' in configure_publish:
    raise SystemExit('new RTSP media can publish delta frames before its IDR')

for needle in ('channel >= MAX_CHANNEL', 'gst_caps_is_fixed(caps)',
               '"framerate"', 'rtsp_h265_annex_b_au_buffer'):
    if needle not in sync:
        raise SystemExit(f'missing caps/AU/rate guard: {needle}')
for needle in ('RTSP_FRAME_ID_COUNTRY_CODE 0xff',
               'RTSP_FRAME_ID_COUNTRY_EXTENSION 0xc1',
               'RTSP_FRAME_ID_REGISTERED_PAYLOAD_SIZE 30',
               'RTSP_FRAME_ID_MAGIC "GSTSYNC1"'):
    if needle not in frame:
        raise SystemExit(f'missing shared SEI envelope: {needle}')
print('sync config source contract: PASSED')
PY
