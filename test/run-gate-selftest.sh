#!/bin/bash
# run-probe-safety-source-check.sh 의 cp 스테이징 판정이 회귀하지 않았는지 검사한다.
#
# 왜 필요한가
#   그 판정은 리뷰에서 **세 라운드 연속 우회**됐다(-t"$D" 붙여쓰기, -ft 묶음, "-t$D"
#   인용, 덮어쓰기를 건너뛰는 -n). PR #124 에서 화이트리스트로 뒤집어 막았지만, 그
#   확인은 일회성 하네스였고 저장소에 남지 않았다. 판정이 다시 느슨해져도 잡을 것이
#   없다. 같은 PR 에서 기존 판정이 **안전한 -f/-fp 까지 거부**하고 있었다는 것도
#   드러났는데, 시험이 있었으면 진작 보였을 결함이다.
#
# 무엇을 검사하나
#   격리 사본의 probe 한 벌에서 스테이징 줄만 13 종 cp 철자로 바꿔 넣고 게이트를
#   **실제로 실행**해, 통과/거부가 기대표와 같은지 본다. 정규식을 따로 흉내내지
#   않는다 - 흉내내면 그 사본이 또 표류한다.
#
# 무엇을 검사하지 않나 (초록을 커버리지로 읽지 말 것)
#   게이트의 나머지 6 개 검사는 대상이 아니다. cp 판정 하나만 본다.
#   그리고 이것은 소스 문자열 판정의 시험이다 - 스테이징이 실기에서 실제로 동작하는지는
#   말하지 않는다.
#
# 보드 불필요. 파일 읽기/임시 디렉터리만 쓴다.
set -euo pipefail

cd "$(dirname "$0")/.."
GATE=test/run-probe-safety-source-check.sh
BASE=test/probe-default-h265.sh
[ -f "$GATE" ] || { echo "게이트를 찾지 못했습니다: $GATE" >&2; exit 2; }
[ -f "$BASE" ] || { echo "기준 스크립트를 찾지 못했습니다: $BASE" >&2; exit 2; }

# 기준 스테이징 줄을 파일에서 직접 집는다. 전문을 하드코딩하면 '|| { ... }' 꼬리가
# 바뀔 때 무관한 이유로 깨진다. 정확히 한 줄이어야 한다 - 아니면 하네스가 무엇을
# 바꾸는지 모른다는 뜻이므로 조용히 통과시키지 않고 중단한다.
mapfile -t _stage < <(grep -nE '^cp .* "\$BIN" \|\|' "$BASE")
if [ "${#_stage[@]}" -ne 1 ]; then
    echo "기준 스테이징 줄이 ${#_stage[@]} 건입니다 (1 건이어야 함): $BASE" >&2
    printf '  %s\n' "${_stage[@]}" >&2
    exit 2
fi
ORIG_LINE=${_stage[0]#*:}
TAIL="|| ${ORIG_LINE#*|| }"     # '|| { echo "중단: ..."; exit 2; }'

# 철자|기대  (기대: OK=게이트 통과해야 함, NG=게이트가 거부해야 함)
#
# OK 인 것들은 "$BIN" 이 목적지이고 실패가 치명적인 형태다.
# NG 인 것들은 둘 중 하나다 - "$BIN" 이 목적지가 아니거나(-t 계열), 복사를 건너뛰고도
# 0 을 반환하거나(-n/-u), 실패가 치명적이지 않거나(|| 없음), 스테이징이 아예 없다.
CASES=$(cat <<'CASES'
현행(정본)|OK|cp /usr/local/bin/gstApp "$BIN"
-f 옵션|OK|cp -f /usr/local/bin/gstApp "$BIN"
-fp 묶음|OK|cp -fp /usr/local/bin/gstApp "$BIN"
인용 소스|OK|cp "$SRC" "$BIN"
-t 붙여쓰기|NG|cp -t"$D" "$BIN"
-t 인용위장|NG|cp "-t$D" "$BIN"
-t 띄어쓰기|NG|cp -t "$D" "$BIN"
-ft 묶음|NG|cp -ft "$D" "$BIN"
--target-directory=|NG|cp --target-directory="$D" "$BIN"
-n 덮어쓰기건너뜀|NG|cp -n /usr/local/bin/gstApp "$BIN"
-u 갱신시만|NG|cp -u /usr/local/bin/gstApp "$BIN"
CASES
)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail=0
total=0
printf '%-26s %-4s %-8s %s\n' "철자" "기대" "게이트" "판정"
printf '%-26s %-4s %-8s %s\n' "--------------------------" "----" "--------" "----"

run_case() {                    # $1=이름 $2=기대 $3=치환할 줄(빈 문자열이면 줄 삭제)
    local name=$1 want=$2 line=$3
    local d="$WORK/case"
    rm -rf "$d"; mkdir -p "$d"
    cp -r test "$d/test"
    rm -f "$d/test/$(basename "$0")"     # 자기 자신은 대상에서 뺀다
    python3 - "$d/test/$(basename "$BASE")" "$ORIG_LINE" "$line" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(path, encoding='utf-8').read()
if s.count(old) != 1:
    sys.exit(f"치환 대상 {s.count(old)} 건 - 하네스 오류")
open(path, 'w', encoding='utf-8').write(
    s.replace(old, new) if new else s.replace(old + "\n", ""))
PY
    local out rc got
    out=$(bash "$d/test/$(basename "$GATE")" 2>&1) && rc=0 || rc=$?
    if [ "$rc" -eq 0 ]; then
        got=ACCEPT
    elif printf '%s' "$out" | grep -q '스테이징'; then
        got=REJECT
    else
        # cp 판정이 아닌 다른 검사가 깨졌다. 이 시험이 무엇을 재는지 알 수 없으므로
        # 통과시키지 않는다.
        printf '%-26s %-4s %-8s %s\n' "$name" "$want" "rc=$rc" "하네스오류"
        printf '%s\n' "$out" | tail -3 | sed 's/^/      /'
        return 1
    fi
    local want_got; [ "$want" = OK ] && want_got=ACCEPT || want_got=REJECT
    if [ "$got" = "$want_got" ]; then
        printf '%-26s %-4s %-8s %s\n' "$name" "$want" "$got" "ok"
        return 0
    fi
    printf '%-26s %-4s %-8s %s\n' "$name" "$want" "$got" "!! 불일치"
    return 1
}

while IFS='|' read -r name want line; do
    [ -n "$name" ] || continue
    total=$((total + 1))
    run_case "$name" "$want" "$line $TAIL" || fail=$((fail + 1))
done <<< "$CASES"

# '||' 가 없으면 실패가 치명적이지 않다. 위 표와 달리 꼬리를 붙이지 않는다.
total=$((total + 1))
run_case "|| 없음" NG 'cp /usr/local/bin/gstApp "$BIN"' || fail=$((fail + 1))
# 스테이징 줄 자체가 사라진 경우. 게이트가 실제로 발화하는지 보는 온전성 검사다.
total=$((total + 1))
run_case "줄 삭제" NG '' || fail=$((fail + 1))

echo
if [ "$fail" -ne 0 ]; then
    echo "gate selftest: cp 판정 $total 종 중 $fail 종 불일치 -> FAILED" >&2
    exit 1
fi
echo "gate selftest: cp 판정 $total 종 전부 기대와 일치 -> PASSED"
