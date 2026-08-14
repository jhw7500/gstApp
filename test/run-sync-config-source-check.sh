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
import re
from pathlib import Path
s = Path('rtspServerBin.cpp').read_text(encoding='utf-8')
sync = Path('rtspSync.cpp').read_text(encoding='utf-8')
frame = Path('rtspFrameId.h').read_text(encoding='utf-8')
main = Path('main.cpp').read_text(encoding='utf-8')
audio = Path('audioBin.cpp').read_text(encoding='utf-8')
encoder = Path('encoderBin.cpp').read_text(encoding='utf-8')
parser = Path('parser.cpp').read_text(encoding='utf-8')
parser_h = Path('parser.h').read_text(encoding='utf-8')
util_h = Path('util.h').read_text(encoding='utf-8')
video = Path('videoBin.cpp').read_text(encoding='utf-8')

missing_v4l2_frame_log = []
for label, needle, source in (
        ('V4L2 frame-log CmdArg field',
         'gboolean v4l2_sync_log_frames;', util_h),
        ('V4L2 frame-log default',
         '#define DEFAULT_V4L2_SYNC_LOG_FRAMES FALSE', parser_h),
        ('V4L2 frame-log default assignment',
         'arg.v4l2_sync_log_frames = DEFAULT_V4L2_SYNC_LOG_FRAMES;', parser),
        ('V4L2 frame-log CLI option name',
         '{"v4l2-sync-log-frames"', parser),
        ('V4L2 trace frame-log state', 'gboolean log_frames;', video),
        ('V4L2 trace frame-log copy',
         'trace->log_frames = cmdArg.v4l2_sync_log_frames;', video),
        ('V4L2 per-frame log guard', 'if (trace->log_frames)', video),
        ('V4L2 trace enabled notice', 'frame_log=%d', video)):
    if needle not in source:
        missing_v4l2_frame_log.append(label)
if missing_v4l2_frame_log:
    raise SystemExit('missing source contracts: ' +
                     ', '.join(missing_v4l2_frame_log))

option_marker = '{"v4l2-sync-log-frames"'
if parser.count(option_marker) != 1:
    raise SystemExit('V4L2 frame-log GOption entry must be unique')
option_start = parser.index(option_marker)
option_end = parser.index('},', option_start) + 2
option_entry = parser[option_start:option_end]
if not re.fullmatch(
        r'\{"v4l2-sync-log-frames"\s*,\s*0\s*,\s*0\s*,\s*'
        r'G_OPTION_ARG_INT\s*,\s*&arg\.v4l2_sync_log_frames\s*,\s*'
        r'"[^"]*"\s*,\s*"INT"\s*\},', option_entry, re.DOTALL):
    raise SystemExit(
        'V4L2 frame-log GOption must use G_OPTION_ARG_INT and '
        '&arg.v4l2_sync_log_frames')

frame_log_assignments = re.findall(
    r'\btrace->log_frames\s*=(?!=)\s*[^;]+;', video)
if len(frame_log_assignments) != 1 or \
        frame_log_assignments[0].strip() != \
        'trace->log_frames = cmdArg.v4l2_sync_log_frames;':
    raise SystemExit(
        'V4L2 trace log_frames must have exactly one normalized CmdArg copy')

trace_normalize = parser.index(
    'sync_trace_sanity(&arg.v4l2_sync_trace_sec, "v4l2_sync_trace_sec")')
bool_validate = parser.index(
    'arg.v4l2_sync_log_frames != FALSE &&\n'
    '      arg.v4l2_sync_log_frames != TRUE')
dependency_normalize = parser.index(
    'arg.v4l2_sync_log_frames && arg.v4l2_sync_trace_sec == 0')
effective_log = parser.index('sync_config v4l2_trace_sec:%d v4l2_log_frames:%d')
if not trace_normalize < bool_validate < dependency_normalize < effective_log:
    raise SystemExit('V4L2 frame-log normalization order is not trace/bool/dependency/log')
if parser.count('arg.v4l2_sync_log_frames = FALSE;',
                bool_validate, effective_log) != 2:
    raise SystemExit('V4L2 invalid/dependency normalization does not disable frame logs')

probe_start = video.index('static GstPadProbeReturn v4l2_sync_trace_probe')
probe_end = video.index('static void install_v4l2_sync_trace', probe_start)
probe = video[probe_start:probe_end]
frame_log_guard = probe.index('if (trace->log_frames)')
frame_log_call = probe.index(
    '__LOG(LOG_NOTICE,\n          "[V4L2_SYNC]', frame_log_guard)
frame_log_guard_end = probe.index('\n  }', frame_log_guard)
if frame_log_call > frame_log_guard_end:
    raise SystemExit('V4L2 per-frame __LOG call is not guarded by log_frames')

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

enc_stat = encoder[encoder.index('typedef struct {'):
                   encoder.index('} EncStat;')]
for field in ('q_in', 'q_out', 'enc_out', 'overrun', 'lvl_buf_max',
              'enc_gap_max_us'):
    if field in enc_stat:
        raise SystemExit(f'raw shared encoder counter remains in EncStat: {field}')
if 'EncoderTelemetry telemetry;' not in enc_stat:
    raise SystemExit('EncStat does not own EncoderTelemetry')

for callback, record_call in (
        ('enc_q_in_probe', 's->telemetry.recordQueueInput()'),
        ('enc_q_out_probe', 'telemetry.recordQueueOutput()'),
        ('enc_out_probe', 's->telemetry.recordEncoderOutput('),
        ('enc_queue_overrun_cb', 'telemetry.recordOverrun()')):
    start = encoder.index(callback)
    body = encoder[start:encoder.index('\n}', start)]
    if record_call not in body:
        raise SystemExit(f'{callback} does not use EncoderTelemetry')

q_in = encoder[encoder.index('enc_q_in_probe'):
               encoder.index('\n}', encoder.index('enc_q_in_probe'))]
if 's->telemetry.recordQueueLevel(lvl)' not in q_in:
    raise SystemExit('queue watermark does not use EncoderTelemetry')

nodata = encoder[encoder.index('enc_nodata_watch'):
                 encoder.index('\n}', encoder.index('enc_nodata_watch'))]
if 'telemetry.snapshot(FALSE)' not in nodata:
    raise SystemExit('no-data watch does not read an encoder telemetry snapshot')

report = encoder[encoder.index('enc_stat_report'):
                 encoder.index('\n}', encoder.index('enc_stat_report'))]
if report.count('EncoderTelemetrySnapshot snapshot = ') != 1 or \
        report.count('s->telemetry.snapshot(TRUE)') != 1:
    raise SystemExit('encoder report must take one resetting local snapshot')
previous = {
    'q_in': 'prev_in',
    'q_out': 'prev_out',
    'enc_out': 'prev_enc',
    'overrun': 'prev_over',
}
for field, prev in previous.items():
    if f'snapshot.{field} - {prev}[i]' not in report or \
            f'{prev}[i] = snapshot.{field}' not in report:
        raise SystemExit(f'encoder report does not reuse snapshot for {field}')
if 'snapshot.lvl_buf_max' not in report or \
        'snapshot.enc_gap_max_us' not in report:
    raise SystemExit('encoder report maxima do not come from the local snapshot')

for field in ('q_in', 'q_out', 'enc_out', 'overrun', 'lvl_buf_max',
              'enc_gap_max_us'):
    if f's->{field}' in encoder:
        raise SystemExit(f'raw EncStat counter access remains: {field}')

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
