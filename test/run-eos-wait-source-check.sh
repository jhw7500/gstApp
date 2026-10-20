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


main_source = Path('main.cpp').read_text(encoding='utf-8')
main = without_comments(main_source)

checks = 0

# 종료 블록: main_end: 레이블부터 NULL 전환까지.
end_label = main.find('main_end:')
if end_label < 0:
    fail('missing source contract: main_end: label')
null_change = main.find('gst_element_set_state(pipeline, GST_STATE_NULL);', end_label)
if null_change < 0:
    fail('missing source contract: gst_element_set_state(pipeline, GST_STATE_NULL) after main_end:')
shutdown = main[end_label:null_change]

# 1) EOS 대기는 bus 에서 EOS|ERROR|ELEMENT 를 꺼낸다. ELEMENT 가 빠지면
#    gst_bus_timed_pop_filtered 가 마지막 조각의 splitmuxsink-fragment-closed 를
#    꺼내 버려 handleFragmentClosed 와 .all_done 표식이 생기지 않는다(#142 1번).
pop = re.search(
    r'gst_bus_timed_pop_filtered\s*\(\s*eos_bus\s*,(.*?)\)\s*;', shutdown, flags=re.DOTALL)
if not pop:
    fail('shutdown must pop the bus with gst_bus_timed_pop_filtered(eos_bus, ...)')
mask = pop.group(1)
for needed in ('GST_MESSAGE_EOS', 'GST_MESSAGE_ERROR', 'GST_MESSAGE_ELEMENT'):
    if needed not in mask:
        fail(f'EOS wait mask must include {needed} — '
             '마스크 밖 메시지는 꺼내서 버려지므로 ELEMENT(fragment-closed)가 유실된다')
checks += 1

# 2) 꺼낸 ELEMENT 는 bus watch 와 같은 경로(bus_message_parse, threadArgs)로 넘긴다.
#    직접 구조체를 해석해 중복 구현하면 worker 큐·health 기록 경로와 어긋난다.
elem = re.search(
    r'GST_MESSAGE_TYPE\s*\(\s*msg\s*\)\s*==\s*GST_MESSAGE_ELEMENT\s*\)\s*\{(.*?)\}',
    shutdown, flags=re.DOTALL)
if not elem:
    fail('EOS wait must branch on GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT')
if not re.search(r'bus_message_parse\s*\(\s*eos_bus\s*,\s*msg\s*,\s*threadArgs\s*\)', elem.group(1)):
    fail('ELEMENT popped during the EOS wait must be passed to '
         'bus_message_parse(eos_bus, msg, threadArgs)')
checks += 1

# 3) ERROR 는 기록만 하고 남은 예산 안에서 EOS 를 계속 기다린다(#142 2번).
#    gst_bus_timed_pop_filtered 는 먼저 온 ERROR 에서 돌아오고 EOS 는 bus 에 남으므로
#    여기서 대기를 끝내면 다른 채널의 마지막 조각 마무리를 끊는다.
err = re.search(
    r'GST_MESSAGE_TYPE\s*\(\s*msg\s*\)\s*==\s*GST_MESSAGE_ERROR\s*\)\s*\{(.*?)\n\s*\}\n',
    shutdown, flags=re.DOTALL)
if not err:
    fail('EOS wait must branch on GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR')
err_body = err.group(1)
if not re.search(r'\bcontinue\s*;', err_body):
    fail('ERROR popped during the EOS wait must "continue" the wait — '
         'ERROR 즉시 중단이면 뒤따르는 EOS 를 받지 못한다')
if re.search(r'\bbreak\s*;', err_body):
    fail('ERROR branch of the EOS wait must not break out of the wait')
checks += 1

# 4) 대기는 EOS_WAIT_SEC 로 묶인 단조 시계 예산을 쓰는 루프여야 한다.
#    루프 없이 한 번만 꺼내면 2)·3) 뒤에 EOS 를 기다릴 수 없고, 예산이 없으면
#    ERROR·ELEMENT 가 이어질 때 종료 시간이 한도를 넘는다(cam_stop_process KILL 5초).
if not re.search(r'while\s*\(\s*TRUE\s*\)', shutdown):
    fail('EOS wait must be a while (TRUE) loop so ELEMENT/ERROR can continue waiting')
if 'g_get_monotonic_time()' not in shutdown or 'EOS_WAIT_SEC' not in shutdown:
    fail('EOS wait loop must bound itself with a g_get_monotonic_time() deadline '
         'derived from EOS_WAIT_SEC')
checks += 1

# 5) 순서 불변식: worker 정지와 threadArgs 해제는 대기 뒤, NULL 전환 앞이어야 한다.
#    대기보다 앞이면 2)가 해제된 threadArgs 를 쓰고 큐가 없어 fragment-closed 를
#    직접 처리하지만 worker join 이 없어 .all_done 이 NULL 전환 전에 보장되지 않는다.
stop_worker = shutdown.find('stop_fragment_closed_worker();')
free_args = shutdown.find('g_free(threadArgs);')
if stop_worker < 0 or free_args < 0:
    fail('shutdown must still call stop_fragment_closed_worker() and g_free(threadArgs)')
if stop_worker < pop.end() or free_args < pop.end():
    fail('stop_fragment_closed_worker() and g_free(threadArgs) must come after the '
         'EOS wait (gst_bus_timed_pop_filtered) and before GST_STATE_NULL — '
         '대기 중 큐에 넣은 fragment-closed 가 join 으로 처리된 뒤 NULL 로 가야 한다')
if stop_worker > free_args:
    fail('stop_fragment_closed_worker() must precede g_free(threadArgs)')
checks += 1

print(f'eos-wait source contract: {checks} checks passed')
PY
