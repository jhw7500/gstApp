#!/bin/bash
# 채널 간 스큐 판정이 '벽시계'가 아니라 '내용(running-time)' 기준인지 소스에서 검증한다.
#
# 배경: split_msec 은 format_location 이 파일을 여는 순간의 벽시계라 스트리밍 스레드
#       스케줄링 지연을 그대로 담는다. 판정 대상은 파일 내용의 경계이므로
#       splitmuxsink-fragment-opened 의 running-time 을 써야 한다.
#       (running-time 이 내용 기준이라는 것은 tee -> splitmuxsink 2개 실측으로 확인:
#        같은 버퍼를 받은 두 sink 가 비트 단위로 동일한 값을 보고했다.)
set -euo pipefail
cd "$(dirname "$0")/.."

fail() { echo "split skew source contract: $1" >&2; exit 1; }

# 1. 저장 필드와 접근자
grep -q 'GstClockTime split_running_time;' muxSinkBin.h || fail "MuxSinkData.split_running_time 없음"
grep -q 'GstClockTime getSplitRunningTime();' muxSinkBin.h || fail "getSplitRunningTime 선언 없음"
grep -q 'void setSplitRunningTime(GstClockTime rt);' muxSinkBin.h || fail "setSplitRunningTime 선언 없음"

# 2. fragment-opened 가 실제로 값을 채우는가
grep -q 'muxSinkData.split_running_time = running_time;' muxSinkBin.cpp \
  || fail "handleFragmentOpened 가 split_running_time 을 채우지 않음"

# 3. 생성자에서 정확히 한 번 UNSET 으로 초기화되는가
#    '>= N' 로 세면 같은 줄이 중복 삽입돼도 통과한다 (실제로 한 번 놓쳤다). 정확히 1회로 고정한다.
init_cnt=$(grep -c 'muxSinkData.split_running_time = GST_CLOCK_TIME_NONE;' muxSinkBin.cpp || true)
[ "$init_cnt" = "1" ] || fail "split_running_time 초기화가 1회가 아님 (실제 ${init_cnt}회)"

# 4. splitCheck 가 running-time 을 스큐 기준으로 쓰는가, 그리고 벽시계 fallback 을 남겼는가
grep -q 'getSplitRunningTime()' main.cpp || fail "splitCheck 가 running-time 을 읽지 않음"
grep -q 'skew_basis = "running-time"' main.cpp || fail "running-time 기준 분기 없음"
grep -q 'skew_basis = "wall-clock"' main.cpp || fail "벽시계 fallback 분기 없음"
grep -q 'rt_usable' main.cpp || fail "running-time 가용성 판정(rt_usable) 없음"

# 5. 정시성(절대 오차) 판정은 여전히 벽시계 기준이어야 한다 — 두 목적을 섞지 않는다
grep -q 'drift_ms >= cmdArg.split_max_msec' main.cpp \
  || fail "절대 오차 판정이 벽시계(drift_ms) 기준이 아님"

# 6. 스냅백 로그가 두 기준을 함께 남기는가 (현장 로그로 사후 분리 가능해야 함)
grep -q 'skew:%dms/%s, wall-skew:%dms' main.cpp || fail "스냅백 로그에 두 기준이 함께 없음"

# 6b. 판정 기준이 NOTICE 로 남는가.
#     운영 log_level 은 NOTICE(5) 라 DEBUG 는 타겟에 보이지 않는다(2026-08-23 실측 확인).
#     기준을 DEBUG 로만 남기면 현장에서 running-time/wall-clock 구분이 불가능하다.
#     'NOTICE 가 어딘가 있다' + 'skew basis 문자열이 어딘가 있다' 를 따로 보면
#     둘이 같은 호출인지 확인되지 않는다(실제로 한 번 놓쳤다). 같은 __LOG 호출 안인지 본다.
python3 - <<'PYCHK'
import re, sys
from pathlib import Path
src = Path('main.cpp').read_text(encoding='utf-8')
m = re.search(r'__LOG\(\s*([A-Z_]+)\s*,[^;]*?skew basis: %s', src, re.S)
if not m:
    print("split skew source contract: 판정 기준 로그(skew basis)가 __LOG 호출 안에 없음", file=sys.stderr)
    sys.exit(1)
if m.group(1) != 'LOG_NOTICE':
    print("split skew source contract: 판정 기준 로그가 %s 임 — 운영 log_level(NOTICE)에서 보이지 않는다"
          % m.group(1), file=sys.stderr)
    sys.exit(1)
print("  skew basis log level ok (%s)" % m.group(1))
PYCHK
#     단 매분 찍으면 안 된다 — 기준 전환 시에만 남기는 가드가 있어야 한다
grep -q 'reported_basis' main.cpp || fail "기준 전환 가드(reported_basis) 없음"

# 7. 리셋 짝 맞추기 — setSplitMsec(SPLIT_MSEC_UNSET) 마다 running-time 도 UNSET 이어야 한다.
#    빠뜨리면 이전 조각의 running-time 이 남아 '정렬됨'으로 오판할 수 있다.
python3 - <<'PY'
import re, sys
from pathlib import Path
src = Path('main.cpp').read_text(encoding='utf-8').splitlines()
unset = [i for i, l in enumerate(src) if 'setSplitMsec(SPLIT_MSEC_UNSET)' in l]
if not unset:
    print("split skew source contract: setSplitMsec(SPLIT_MSEC_UNSET) 호출이 없음", file=sys.stderr)
    sys.exit(1)
bad = []
for i in unset:
    window = "\n".join(src[i:i + 3])
    if 'setSplitRunningTime(GST_CLOCK_TIME_NONE)' not in window:
        bad.append(i + 1)
if bad:
    print("split skew source contract: running-time 리셋이 빠진 줄 %s" % bad, file=sys.stderr)
    sys.exit(1)
print("  reset pairing ok (%d sites)" % len(unset))
PY

echo "split skew source contract: PASSED"
