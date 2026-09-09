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


mux_source = Path('muxSinkBin.cpp').read_text(encoding='utf-8')
mux = without_comments(mux_source)

checks = 0

# 1) 임계 상수가 있고 값이 5000ms 다.
#    현장 로그에서 5초 미만 조각은 0건, 최단 약 8.2초. 이슈 #94 피해 조각은 0.37~0.57초.
#    값을 올리면 정상 조각을 사건으로 오탐하고, 내리면 #94 계열을 놓친다.
m = re.search(r'#define\s+SHORT_FRAGMENT_ERR_MSEC\s+(\d+)', mux)
if not m:
    fail('missing source contract: #define SHORT_FRAGMENT_ERR_MSEC')
if m.group(1) != '5000':
    fail(f'SHORT_FRAGMENT_ERR_MSEC must stay 5000ms (found {m.group(1)}) — '
         '현장 기준선(5초 미만 0건 / 최단 8.2초)에 맞춘 값이다')
checks += 1

closed = function_body(
    mux,
    'void MuxSinkBin::handleFragmentClosed(const gchar *location, '
    'GstClockTime running_time, GstClockTime duration)')

# 2) 짧은 조각 관측이 handleFragmentClosed 안에 있다.
if 'SHORT_FRAGMENT_ERR_MSEC' not in closed:
    fail('handleFragmentClosed() must observe short fragments — '
         'SHORT_FRAGMENT_ERR_MSEC 비교가 사라졌다')
checks += 1

# 3) LOG_ERR 이어야 한다. 운영 rsyslog 는 local0.notice 이상만 파일로 보내므로
#    INFO/DEBUG 로 내리면 타겟 로그에서 사라지고 관측 자체가 무효가 된다.
short_stmt = re.search(
    r'if\s*\([^;{]*SHORT_FRAGMENT_ERR_MSEC[^;{]*\)\s*\{(.*?)\}',
    closed, flags=re.DOTALL)
if not short_stmt:
    fail('short-fragment observation must be a single guarded if-block')
body = short_stmt.group(1)
if 'LOG_ERR' not in body:
    fail('short-fragment observation must log at LOG_ERR — '
         '운영 rsyslog 가 local0.notice 이상만 파일로 보낸다(INFO/DEBUG 는 보이지 않음)')
for weak in ('LOG_INFO', 'LOG_DEBUG', 'LOG_NOTICE'):
    if weak in body:
        fail(f'short-fragment observation must not use {weak} — LOG_ERR 만 유효하다')
checks += 1

# 4) 첫 조각 제외 가드. last_end_time 이 invalid 면 이 채널의 첫 조각 닫힘이고,
#    기동 시각에 따라 정상적으로 0~60초 아무 길이나 될 수 있다(실측: 기동 8초 조각).
#    가드가 없으면 정상 런에서도 매번 오탐한다.
guard = short_stmt.group(0)
guard_cond = guard[:guard.index('{')]
if 'last_end_time' not in guard_cond:
    fail('short-fragment guard must exclude the first fragment via '
         'GST_CLOCK_TIME_IS_VALID(muxSinkData.last_end_time) — '
         '없으면 기동 부분조각을 매번 오탐한다')
if 'GST_CLOCK_TIME_IS_VALID' not in guard_cond:
    fail('short-fragment guard must validate the clock times it compares')
checks += 1

# 5) 순서 불변식: 관측이 last_end_time 대입보다 앞에 와야 4)의 판별이 성립한다.
#    대입이 먼저 오면 last_end_time 은 항상 valid 가 되어 첫 조각 제외가 죽는다.
assign = re.search(r'muxSinkData\.last_end_time\s*=\s*running_time\s*;', closed)
if not assign:
    fail('handleFragmentClosed() must still record muxSinkData.last_end_time')
if short_stmt.start() > assign.start():
    fail('short-fragment observation must precede '
         '"muxSinkData.last_end_time = running_time;" — '
         '뒤에 두면 last_end_time 이 항상 valid 가 되어 첫 조각 제외가 무력화된다')
checks += 1

print(f'short-fragment source contract: {checks} checks passed')
PY
