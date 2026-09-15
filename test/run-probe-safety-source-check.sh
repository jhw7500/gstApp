#!/bin/bash
# 타겟 실행 스크립트의 운영 안전 불변식을 소스에서 검사한다.
#
# 왜 필요한가
#   test/probe-*.sh 11 종과 run-fps-scenario.sh, run-skew55a.sh 는 한 조사에서 복사·편집으로
#   파생돼 안전
#   장치가 파일마다 인라인으로 중복돼 있다. 실제로 pre-PR 리뷰에서 나온 결함 5 건을 고칠 때
#   **같은 수정을 11 번** 넣어야 했고, 한 곳이라도 빠지면 그 스크립트만 조용히 위험한 채로
#   남는다. 스크립트를 하나로 접는 리팩터는 하지 않는다 — 각 파일이 문서(§5.8.7)와 max9296
#   이슈 #64/#65 에서 특정 측정을 낸 도구로 이름·원자료 태그와 함께 인용된 증거물이라,
#   합치면 발행된 수치를 낸 산출물과 더 이상 일치하지 않는다. 대신 여기서 함께 검사한다.
#
# 이 시험이 막는 것과 막지 못하는 것 (과장하지 않는다)
#   막는다   안전 장치의 **삭제·누락·되돌림**, 그리고 새 파일이 장치 없이 파생되는 것.
#            dirty 플래그가 쓰기 뒤로 밀리는 특정 회귀(아래 2b)도 잡는다.
#   못 막는다 의미적 약화 — 도달 불가능한 분기 안의 대입(`if false; then CONF_DIRTY=1; fi`),
#            조건을 항상 참으로 만든 표류 검사 등. 토큰이 있으면 통과한다.
#            여기 통과는 "장치가 소스에 남아 있다" 는 뜻이지 "동작한다" 는 뜻이 아니다.
#            5 번의 UNCONDITIONAL_CAM_RESTART 목록은 **사람이 주장한 사실**이다 — 그 파일에
#            정지 상태로 두는 경로가 없다는 것을 게이트가 구조적으로 검증하지는 못한다.
#
# 검사하는 불변식 (전부 실기 파괴로 이어졌던 실제 결함에서 나왔다)
#   1. 표류 검사   live config 가 백업과 다르면 첫 쓰기 전에 중단 (ALLOW_CONF_DRIFT 로만 우회)
#   2. dirty 플래그 config 를 실제로 덮어쓴 회차에서만 복원한다
#   2b. 쓰기 전 세움 플래그가 config 쓰기 **바로 앞** 이다. 뒤에 있으면 쓰기 도중 SIGINT 로
#                  죽었을 때 플래그가 0 이라 복원이 생략되고, 프로브 config(또는 잘린 JSON)가
#                  운영 설정으로 남는다 — 과잉 복원을 고치다 만든 실제 회귀다.
#   3. 삭제 한정   녹화 삭제를 이 실행이 만든 파일로 한정 (-mmin 고정 창 금지)
#   4. 스테이징    앱 복사·chmod 실패는 치명적 (낡은 바이너리 측정 금지)
#   5. cam-operate 되살리는 경로가 반드시 있고, 정지 상태로 두는 선택지를 가진 스크립트는
#                  그 사실을 크게 알린다(RESTORE_CAM_OPERATE 로 되살릴 수 있다). 무조건
#                  되살리는 스크립트에 그 안내를 요구하지 않는다 — 거짓 양성이 된다.
#   6. trap        EXIT/INT/TERM 에 복원이 걸려 있고, 이후 해제되지 않는다
#
# 이슈 #113 관련 (대상이 다르다 - put_conf 를 정의한 스크립트 전부, 오늘 13 개)
#   7. put_conf 동일성 정의가 파일당 정확히 하나이고, 한 줄 형태이고, 모든 파일에서
#      바이트 동일하다. 13 벌 복제가 저장소 정책이므로(위 참조) 한 벌만 손대는 표류를
#      이렇게 잡는다. 대상은 put_conf 를 정의하거나 호출하는 스크립트 전부다 - 정의
#      철자를 바꿔 대상에서 빠지는 회피를 막으려고 호출까지 본다.
#
# 여기서 **하지 않는** 것 (초록을 커버리지로 읽지 말 것)
#   - CONF 가 gstApp 이 읽는 병합 문서를 가리키는지 **검사하지 않는다**. 검사해 봤으나
#     들여쓴 재대입, export/declare/readonly, 중괄호 그룹 안 대입이 전부 빠져나갔다
#     (리뷰 실측). 셸에서 유효한 값은 마지막 대입이라 소스에서 건전히 판정할 수 없다.
#   - config 쓰기가 전부 put_conf 를 거치는지 검사하지 않는다. `${CONF}` 중괄호형,
#     따옴표 없는 $CONF, 변수 별칭, `sed -i`, `>|`, `eval`, heredoc 이 빠져나갔고,
#     본문 주석 한 줄이나 불균형 중괄호 하나로 검사 자체가 꺼졌다.
#   - 시험 쓰기가 `|| exit N` 을 가지는지 검사하지 않는다. 첫 일치만 보므로 둘째 쓰기를
#     못 보고, `|| exit 0` 도 통과했다.
#   - put_conf 본문이 무엇을 하는지 검사하지 않는다. 7 번은 13 벌이 서로 같은지만 본다.
#   이 계약들은 소스 검사가 아니라 docs 의 기록과 실행 검증으로 지킨다.
set -euo pipefail

cd "$(dirname "$0")/.."

python3 - <<'PY'
import re
import sys
from pathlib import Path

# 프로브뿐 아니라 가장 많이 쓰는 하네스(run-fps-scenario.sh)도 같은 계약을 진다.
# 대상에서 빠져 있던 동안 이 파일에만 결함 3 건이 남은 채 게이트는 초록이었다.
targets = sorted(Path("test").glob("probe-*.sh"))
harness = Path("test/run-fps-scenario.sh")
if not harness.exists():
    raise SystemExit(f"{harness} 를 찾지 못했습니다")
targets.append(harness)
skew = Path("test/run-skew55a.sh")
if not skew.is_file():
    print("test/run-skew55a.sh 를 찾지 못했습니다", file=sys.stderr)
    raise SystemExit(1)
targets.append(skew)
if len(targets) < 2:
    raise SystemExit("검사 대상 스크립트를 찾지 못했습니다")

# put_conf 를 정의**하거나 호출**하는 스크립트 전부가 7 번의 대상이다. 이름을 하드코딩
# 하지 않으므로 파생 러너도, 하위 디렉터리도 자동으로 들어온다. 호출까지 보는 이유:
# 정의 탐지만으로 고르면 정의 철자를 바꾸는 것만으로 대상에서 빠져 검사가 꺼진다.
_PC_DEF = re.compile(r"^[ \t]*put_conf[ \t]*\([ \t]*\)[ \t]*\{")
_PC_CALL = re.compile(r'^[ \t]*put_conf "', re.M)   # re.M 없으면 파일 0 번 위치만 본다


def _defs(source):
    return [l for l in source.splitlines() if _PC_DEF.match(l)]


runtime_targets = sorted(
    p for p in Path("test").rglob("*.sh")
    if (lambda src: bool(_defs(src)) or _PC_CALL.search(src) is not None)(
        p.read_text(encoding="utf-8"))
)
if len(runtime_targets) < 2:
    raise SystemExit("put_conf 를 쓰는 스크립트를 찾지 못했습니다")

# 무조건 cam-operate 를 되살리는 스크립트. 정지 상태로 두는 경로가 없으므로 안내가
# 필요 없다. 아래 항목은 restore 경로를 직접 읽고 확인한 것이다.
UNCONDITIONAL_CAM_RESTART = {
    "run-skew55a.sh",   # restore() 가 systemctl start 를 조건 없이 부른다
}

# cp 가 $BIN 을 **목적지**로 쓰면서 실패를 치명으로 다루는가.
#
# 이 판정은 화이트리스트다. 셸 한 줄에서 cp 의 의미를 문자열로 알아내는 것은 변형을
# 하나씩 막는 싸움이 된다 — 실제로 세 라운드 연속 새 형태가 나왔다(-t"$D" 붙여쓰기,
# -ft 묶음, "-t$D" 인용, 그리고 덮어쓰기를 건너뛰는 -n). 그래서 반대로, **아는 안전한
# 형태만** 인정하고 나머지는 전부 거부한다. 새 철자를 쓰려면 여기를 같이 고쳐야 하고,
# 그 변경은 게이트 파일에 남아 리뷰에 걸린다.
#
#   허용: cp [-f|-p 조합] <소스> "$BIN" || ...
#   소스는 인용/비인용 모두 되지만 '-' 로 시작할 수 없다("-t$D" 같은 위장 차단).
#   -n(--no-clobber)·-u·-t 등은 플래그 집합에 없으므로 거부된다 — -n 은 대상이 이미
#   있으면 복사를 건너뛰고도 0 을 반환해 낡은 바이너리가 측정된다.
_CP_STAGE_OK = re.compile(
    r'^[ \t]*cp(?:[ \t]+-[fp]+)*'
    r'[ \t]+(?:"[^"\n-][^"\n]*"|[^\s"\'|-][^\s"\'|]*)'
    r'[ \t]+"\$BIN"[ \t]*\|\|',
    re.M)


def _stages_bin_fatally(source):
    return _CP_STAGE_OK.search(source) is not None


CHECKS = (
    (
        "표류 검사",
        lambda s, p: 'ALLOW_CONF_DRIFT' in s
        and re.search(r'if \[ "\$LIVE_MD5" != "\$ORIG_MD5" \]', s) is not None,
        "live 와 백업이 다를 때 중단하는 검사가 없습니다 (ALLOW_CONF_DRIFT 우회 포함)",
    ),
    (
        "dirty 플래그",
        lambda s, p: "CONF_DIRTY=0" in s
        and "CONF_DIRTY=1" in s
        and re.search(r'if \[ "\$CONF_DIRTY" -eq 1 \]', s) is not None,
        "config 를 덮어쓰지 않은 중단 경로에서도 복원합니다 (CONF_DIRTY 가드 없음)",
    ),
    (
        "쓰기 전 세움",
        # CONF_DIRTY=1 은 config 를 덮어쓰는 줄의 **바로 앞 줄**이어야 한다.
        # 뒤에 두면 그 쓰기가 SIGINT 로 죽었을 때 플래그가 0 이라 복원이 생략된다.
        # 쓰기 형태는 맨 cp 였다가 이슈 #113 에서 put_conf 로 바뀌었다 — 서비스를
        # 멈추면 systemd 가 RuntimeDirectory 를 지우므로 쓰기 전에 되살려야 한다.
        # 두 형태를 다 받되 **인접** 요구는 완화하지 않는다. 완화하면 이 검사가
        # 막으려던 회귀(플래그를 쓰기 뒤로 미는 것)가 그대로 통과한다.
        # 줄 끝에 고정하지 않는다 (뒤에 '|| exit 2' 나 '; sync' 가 붙을 수 있다).
        # 인접 요구는 그대로다 — 그것이 이 검사의 전부다.
        lambda s, p: re.search(
            r'^[ \t]*CONF_DIRTY=1\n'
            r'[ \t]*(?:cp "[^"]*" "\$CONF"|put_conf "[^"]*")',
            s, re.M
        ) is not None,
        "CONF_DIRTY=1 이 config 쓰기(cp 또는 put_conf) 바로 앞에 있지 않습니다 "
        "— 쓰기 도중 죽으면 복원이 생략되고 시험 config 가 운영에 남습니다",
    ),
    (
        "삭제 한정",
        lambda s, p: "-mmin" not in s
        and ('newermt "@$RUN_T0"' not in s or "RUN_T0=" in s),
        "녹화 삭제가 고정 시간창(-mmin)을 씁니다 — 운영 녹화까지 지웁니다",
    ),
    (
        "스테이징 치명화",
        # 성질은 "$BIN 에 쓰는 cp 와 chmod 가 치명적인가" 다. 플래그 개수는 고정하지 않되
        # $BIN 이 **목적지** 여야 한다 — cp -t "$D" "$BIN" 은 $BIN 을 소스로 읽는 명령이라
        # 스테이징이 전혀 일어나지 않는데, 목적지 여부를 보지 않으면 통과한다(실측).
        lambda s, p: _stages_bin_fatally(s)
        and re.search(r'chmod \+x "\$BIN" \|\|', s) is not None,
        "앱 스테이징 실패가 치명적이지 않습니다 — 낡은 바이너리가 측정될 수 있습니다",
    ),
    (
        "cam-operate 복원",
        # 성질은 "정지된 채로 방치하지 않는다" 다. 되살리는 경로가 반드시 있고, 정지 상태로
        # 둘 수 있는 스크립트는 그 사실을 알려야 한다.
        #
        # 예전 판은 RESTORE_CAM_OPERATE 라는 **변수 이름**이 있는지로 "조건부인가" 를
        # 추론했다. 이름을 바꾸면서 안내를 지우면 조건부 분기가 그대로인데도 통과한다.
        # 그래서 무조건 되살리는 파일을 아래 목록으로 **명시**한다. 목록에 넣는 것은
        # "이 파일에는 정지 상태로 두는 경로가 없다" 는 주장이고, 게이트는 그 주장을
        # 구조적으로 검증하지 못한다 — 넣을 때 restore 경로를 직접 읽어 확인할 것.
        lambda s, p: "systemctl start cam-operate" in s
        and (p.name in UNCONDITIONAL_CAM_RESTART or "정지 상태로 둡니다" in s),
        "cam-operate 를 되살리는 경로가 없거나, 정지 상태로 두면서 알리지 않습니다",
    ),
    (
        "trap 복원",
        lambda s, p: re.search(r"^trap restore EXIT INT TERM$", s, re.M) is not None
        and re.search(r"^\s*trap\s+-", s, re.M) is None,
        "EXIT/INT/TERM trap 에 restore 가 걸려 있지 않거나 이후 해제됩니다",
    ),
)


failures = []
# 검사 수는 입력에 의존하지 않는다 — 파일마다 같은 검사를 전부 돌린다.
# (예전 판은 -delete 유무로 개수가 달라져 79/78 을 오갔고, 그래서 개수가 계약의
#  일부인지 우연인지 알 수 없었다.)
checked = len(targets) * (len(CHECKS) + 1) + 1
for path in targets:
    source = path.read_text(encoding="utf-8")
    for name, predicate, message in CHECKS:
        if not predicate(source, path):
            failures.append(f"FAIL {path}: [{name}] {message}")

    # 삭제 한정 보강: -delete 가 있으면 반드시 이 실행의 t0 로 한정한다.
    if "-delete" in source and 'newermt "@$RUN_T0"' not in source:
        failures.append(
            f"FAIL {path}: [삭제 한정] -delete 가 있는데 -newermt \"@$RUN_T0\" 로 한정하지 않습니다"
        )

# 7. put_conf 동일성. 13 벌 복제는 저장소 정책이라 합치지 않는다 - 대신 서로 달라지는
# 것을 여기서 잡는다. 본문이 무엇을 하는지는 보지 않고 "모두 같은가" 만 본다.
defs = {}
for path in runtime_targets:
    lines = _defs(path.read_text(encoding="utf-8"))
    if len(lines) != 1:
        failures.append(
            f"FAIL {path}: [put_conf 동일성] put_conf 정의가 {len(lines)} 개입니다 (1 개여야 합니다)"
        )
        continue
    line = lines[0]
    # 한 줄 정의여야 한다. 여러 줄을 허용하면 본문을 비교하려고 중괄호를 세게 되고,
    # 그 파싱이 조용히 꺼지는 것이 이 검사가 대체한 옛 검사의 실패 방식이었다.
    if not line.startswith("put_conf() {") or not line.rstrip().endswith("}"):
        failures.append(
            f"FAIL {path}: [put_conf 동일성] put_conf 정의가 한 줄의 "
            f"'put_conf() {{ ... }}' 형태가 아닙니다"
        )
        continue
    defs.setdefault(line, []).append(str(path))
if len(defs) > 1:
    groups = " / ".join(
        f"{len(v)}개({v[0]} 외)" if len(v) > 1 else v[0] for v in defs.values()
    )
    failures.append(
        f"FAIL [put_conf 동일성] put_conf 정의가 파일마다 다릅니다 — {groups}"
    )

# 분류기: 입력 헤더를 단언하고 비정상 입력을 거부해야 한다.
awk_path = Path("test/classify-freeze.awk")
if not awk_path.exists():
    failures.append("FAIL test/classify-freeze.awk 가 없습니다")
else:
    awk_source = awk_path.read_text(encoding="utf-8")
    checked += 2
    for name, needle, message in (
        ("헤더 단언", 'EXPECT = "t_us,a_total,a_cnt,b_total,b_cnt,csi_d"',
         "허용 헤더를 단언하지 않습니다 — 다른 레이아웃 CSV 에 오답을 냅니다"),
        ("거부 종료", "exit 2",
         "비정상 입력을 non-zero 로 거부하지 않습니다"),
    ):
        if needle not in awk_source:
            failures.append(f"FAIL {awk_path}: [{name}] {message}")

if failures:
    print("\n".join(failures), file=sys.stderr)
    raise SystemExit(
        f"probe safety source contract: 계약 {len(targets)} 파일 {checked} 검사 / "
        f"put_conf 동일성 {len(runtime_targets)} 파일, {len(failures)} 실패 -> FAILED"
    )

print(
    f"probe safety source contract: 계약 {len(targets)} 파일 {checked} 검사 / "
    f"put_conf 동일성 {len(runtime_targets)} 파일, 0 실패 -> PASSED"
)
PY
