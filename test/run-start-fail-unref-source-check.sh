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


main = without_comments(Path('main.cpp').read_text(encoding='utf-8'))
unref = re.compile(r'gst_object_unref\s*\(\s*pipeline\s*\)')

checks = 0

# 시작 구간: pipeline 생성부터 main_end: 레이블까지. 종료 블록: main_end: 부터 main() 끝까지.
created = main.find('pipeline = gst_pipeline_new(')
if created < 0:
    fail('missing source contract: pipeline = gst_pipeline_new(...)')
end_label = main.find('main_end:', created)
if end_label < 0:
    fail('missing source contract: main_end: label after pipeline creation')
main_close = re.search(r'\n\}[ \t]*\n', main[end_label:])
if not main_close:
    fail('missing source contract: closing brace of main() after main_end:')
startup = main[created:end_label]
shutdown = main[end_label:end_label + main_close.start()]

# 1) 시작 구간에서는 pipeline 을 unref 하지 않는다(#141).
#    이 구간의 실패 분기는 모두 goto main_end 로 가고, 종료 블록이 같은 포인터로
#    EOS 전송·GST_STATE 확인·NULL 전환을 한 뒤 직접 unref 한다. 앞에서 unref 하면
#    해제된 객체를 다시 쓰고(use-after-free) 두 번 unref 한다.
early = len(unref.findall(startup))
if early:
    fail(f'gst_object_unref(pipeline) must not appear between pipeline creation and '
         f'main_end: (found {early}) — 실패 분기는 goto main_end 만 하고 unref 는 '
         '종료 블록에 맡긴다')
checks += 1

# 2) 시작 단계 상태 전환(PAUSED·PLAYING)의 실패 분기는 여전히 goto main_end 로 간다.
#    1) 을 맞추려고 분기를 지우면 실패한 pipeline 으로 다음 단계가 이어진다.
for state in ('GST_STATE_PAUSED', 'GST_STATE_PLAYING'):
    call = re.search(
        r'ret\s*=\s*gst_element_set_state\s*\(\s*pipeline\s*,\s*' + state + r'\s*\)\s*;',
        startup)
    if not call:
        fail(f'missing source contract: ret = gst_element_set_state(pipeline, {state}) '
             'before main_end:')
    rest = startup[call.end():]
    next_call = re.search(r'gst_element_set_state\s*\(', rest)
    segment = rest[:next_call.start()] if next_call else rest
    branch = re.search(
        r'if\s*\(\s*ret\s*==\s*GST_STATE_CHANGE_FAILURE\s*\)\s*\{(.*?)\}',
        segment, flags=re.DOTALL)
    if not branch or not re.search(r'\bgoto\s+main_end\s*;', branch.group(1)):
        fail(f'{state} transition failure branch must still goto main_end')
checks += 1

# 3) 종료 블록은 NULL 전환 뒤 pipeline 을 정확히 한 번 unref 한다.
#    빠지면 pipeline 이 새고, 두 번이면 이중 해제다.
null_change = shutdown.find('gst_element_set_state(pipeline, GST_STATE_NULL);')
if null_change < 0:
    fail('missing source contract: gst_element_set_state(pipeline, GST_STATE_NULL) '
         'after main_end:')
late = [m.start() for m in unref.finditer(shutdown)]
if len(late) != 1:
    fail(f'shutdown must unref pipeline exactly once (found {len(late)})')
if late[0] < null_change:
    fail('shutdown must unref pipeline after gst_element_set_state(pipeline, GST_STATE_NULL)')
checks += 1

print(f'start-fail unref source contract: {checks} checks passed')
PY
