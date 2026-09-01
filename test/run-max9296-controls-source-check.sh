#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

python3 - <<'PY'
import re
from pathlib import Path


def fail(message):
    raise SystemExit(message)


def without_comments(source):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.DOTALL)


def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        fail(f'missing source contract: {signature}')
    opening = source.find('{', start)
    if opening < 0:
        fail(f'missing function body: {signature}')
    depth = 1
    pos = opening + 1
    while pos < len(source) and depth:
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
        pos += 1
    if depth:
        fail(f'unterminated function body: {signature}')
    return source[opening + 1:pos - 1]


video_source = without_comments(
    Path('videoBin.cpp').read_text(encoding='utf-8'))
main_source = without_comments(Path('main.cpp').read_text(encoding='utf-8'))
makefile = Path('Makefile').read_text(encoding='utf-8')

crop = function_body(video_source, 'static int apply_crop_v4l2(')
init = function_body(video_source, 'gboolean VideoBin::init(')
main = function_body(main_source, 'gint main(')

if crop.count('open(') != 1 or crop.count('close(fd)') != 1:
    fail('crop helper must open one subdev fd and close it exactly once')
if re.search(r'if\s*\(\s*close\s*\(\s*fd\s*\)', crop):
    fail('crop helper must not invalidate successful ioctls on close EINTR')
if crop.count('VIDIOC_S_CTRL') != 1:
    fail('crop helper must issue exactly one S_CTRL for crop_enable')
if crop.count('VIDIOC_S_EXT_CTRLS') != 1:
    fail('crop tuple must use exactly one S_EXT_CTRLS transaction')
enable_pos = crop.find('VIDIOC_S_CTRL')
tuple_pos = crop.find('VIDIOC_S_EXT_CTRLS')
if enable_pos < 0 or tuple_pos < 0 or enable_pos >= tuple_pos:
    fail('crop_enable S_CTRL must precede the tuple S_EXT_CTRLS')
for token in ('max9296_crop_build_control_batch(', 'error_idx', 'errno',
              'common_dz', 'centers[0]', 'centers[1]', 'enabled_slots'):
    if token not in crop:
        fail(f'crop helper is missing diagnostics/contract token: {token}')
if not re.search(r'if\s*\([^)]*VIDIOC_S_CTRL[^)]*\)\s*<\s*0\s*\)', crop):
    fail('crop helper must propagate crop_enable ioctl failure')
if not re.search(r'if\s*\([^)]*VIDIOC_S_EXT_CTRLS[^)]*\)\s*<\s*0\s*\)', crop):
    fail('crop helper must propagate crop tuple ioctl failure')

enabled_pos = init.find('enabled_slots =')
auto_pos = init.find('auto_ae_slots =')
plan_pos = init.find('max9296_exposure_plan(')
warn_pos = init.find('MAX9296_WARN_AND_WRITE_EXPOSURE_SEED')
crop_call_pos = init.find('apply_crop_v4l2(')
first_regular_ctrl = init.find('set_v4l2_subdev_control(')
ordered = (
    ('enabled slot mask', enabled_pos),
    ('auto-AE slot mask', auto_pos),
    ('exposure policy', plan_pos),
    ('manual exposure warning', warn_pos),
    ('atomic crop apply', crop_call_pos),
    ('ordinary camera controls', first_regular_ctrl),
)
missing = [label for label, position in ordered if position < 0]
if missing:
    fail('missing VideoBin startup contracts: ' + ', '.join(missing))
for (left_label, left), (right_label, right) in zip(ordered, ordered[1:]):
    if left >= right:
        fail(f'VideoBin order violation: {left_label} must precede {right_label}')

warning_branch = re.search(
    r'if\s*\(\s*exposure_plan\s*==\s*MAX9296_WARN_AND_WRITE_EXPOSURE_SEED\s*\)'
    r'\s*\{(?P<body>.*?)\}', init, flags=re.DOTALL)
if not warning_branch or 'LOG_WARNING' not in warning_branch.group('body'):
    fail('high-FPS manual exposure must emit an operator-visible warning')
if not warning_branch or 'over_period' not in warning_branch.group('body'):
    fail('high-FPS manual exposure warning must report frame-period overflow')
if 'return FALSE' in warning_branch.group('body'):
    fail('high-FPS manual exposure warning must not abort VideoBin::init')
if not re.search(
        r'if\s*\(\s*apply_crop_v4l2\([^;]+\)\s*<\s*0\s*\)'
        r'\s*\{.*?return\s+FALSE\s*;', init, flags=re.DOTALL):
    fail('crop ioctl failure must fail VideoBin::init')
if init.count('V4L2_CID_EXT_TIME') != 1:
    fail('VideoBin::init must have exactly one exposure seed call')
if not re.search(
        r'if\s*\(\s*exposure_plan\s*!=\s*MAX9296_SKIP_EXPOSURE_SEED\s*\)'
        r'\s*\{[^{}]*V4L2_CID_EXT_TIME', init, flags=re.DOTALL):
    fail('exposure seed must be written for safe and warned-manual policies')

video_init_pos = main.find('videoBin[csiNum].init(')
prepare_pos = main.find('max9296_prepare_all(')
if video_init_pos < 0 or prepare_pos < 0 or video_init_pos >= prepare_pos:
    fail('VideoBin initialization (crop apply) must precede MAX9296 prepare')

for forbidden in ('SENSOR-640', 'HD-ISP', 'FHD-ISP',
                  'SENSOR_640', 'HD_ISP', 'FHD_ISP'):
    if forbidden in video_source or forbidden in main_source:
        fail(f'gstApp must not select a scaling/readout pipeline: {forbidden}')

if not re.search(r'^videoBin\.o\s*:.*max9296Controls\.h', makefile,
                 flags=re.MULTILINE):
    fail('videoBin.o must depend on max9296Controls.h')
if not re.search(r'^\$\(OUTPUT\)/testMax9296Controls\s*:', makefile,
                 flags=re.MULTILINE):
    fail('Makefile has no bin/testMax9296Controls target')

print('max9296 crop/exposure source contract: PASSED')
PY
