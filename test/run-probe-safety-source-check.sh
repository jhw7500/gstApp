#!/bin/bash
# 타겟 실행 스크립트의 운영 안전 불변식을 소스에서 검사한다.
#
# 왜 필요한가
#   test/probe-*.sh 11 종과 run-fps-scenario.sh 는 한 조사에서 복사·편집으로 파생돼 안전
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
#
# 검사하는 불변식 (전부 실기 파괴로 이어졌던 실제 결함에서 나왔다)
#   1. 표류 검사   live config 가 백업과 다르면 첫 쓰기 전에 중단 (ALLOW_CONF_DRIFT 로만 우회)
#   2. dirty 플래그 config 를 실제로 덮어쓴 회차에서만 복원한다
#   2b. 쓰기 전 세움 플래그가 cp **바로 앞** 이다. 뒤에 있으면 cp 도중 SIGINT 로 죽었을 때
#                  플래그가 0 이라 복원이 생략되고, 프로브 config(또는 잘린 JSON)가 운영
#                  설정으로 남는다 — 과잉 복원을 고치다 만든 실제 회귀다.
#   3. 삭제 한정   녹화 삭제를 이 실행이 만든 파일로 한정 (-mmin 고정 창 금지)
#   4. 스테이징    앱 복사·chmod 실패는 치명적 (낡은 바이너리 측정 금지)
#   5. cam-operate 정지 상태로 두면 크게 알리고, RESTORE_CAM_OPERATE 로 되살릴 수 있다
#   6. trap        EXIT/INT/TERM 에 복원이 걸려 있고, 이후 해제되지 않는다
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
if len(targets) < 2:
    raise SystemExit("검사 대상 스크립트를 찾지 못했습니다")

CHECKS = (
    (
        "표류 검사",
        lambda s: 'ALLOW_CONF_DRIFT' in s
        and re.search(r'if \[ "\$LIVE_MD5" != "\$ORIG_MD5" \]', s) is not None,
        "live 와 백업이 다를 때 중단하는 검사가 없습니다 (ALLOW_CONF_DRIFT 우회 포함)",
    ),
    (
        "dirty 플래그",
        lambda s: "CONF_DIRTY=0" in s
        and "CONF_DIRTY=1" in s
        and re.search(r'if \[ "\$CONF_DIRTY" -eq 1 \]', s) is not None,
        "config 를 덮어쓰지 않은 중단 경로에서도 복원합니다 (CONF_DIRTY 가드 없음)",
    ),
    (
        "쓰기 전 세움",
        # CONF_DIRTY=1 은 config 를 덮어쓰는 cp 의 **바로 앞 줄**이어야 한다.
        # 뒤에 두면 cp 자신이 SIGINT 로 죽었을 때 플래그가 0 이라 복원이 생략된다.
        lambda s: re.search(
            r'^[ \t]*CONF_DIRTY=1\n[ \t]*cp "[^"]*" "\$CONF"$', s, re.M
        ) is not None,
        "CONF_DIRTY=1 이 config 쓰기 바로 앞에 있지 않습니다 "
        "— cp 도중 죽으면 복원이 생략되고 시험 config 가 운영에 남습니다",
    ),
    (
        "삭제 한정",
        lambda s: "-mmin" not in s
        and ('newermt "@$RUN_T0"' not in s or "RUN_T0=" in s),
        "녹화 삭제가 고정 시간창(-mmin)을 씁니다 — 운영 녹화까지 지웁니다",
    ),
    (
        "스테이징 치명화",
        lambda s: re.search(r'cp \S+ "\$BIN" \|\|', s) is not None
        and re.search(r'chmod \+x "\$BIN" \|\|', s) is not None,
        "앱 스테이징 실패가 치명적이지 않습니다 — 낡은 바이너리가 측정될 수 있습니다",
    ),
    (
        "cam-operate 복원",
        lambda s: "RESTORE_CAM_OPERATE" in s
        and "정지 상태로 둡니다" in s,
        "cam-operate 를 정지 상태로 둘 때 알리지 않거나 되살릴 방법이 없습니다",
    ),
    (
        "trap 복원",
        lambda s: re.search(r"^trap restore EXIT INT TERM$", s, re.M) is not None
        and re.search(r"^\s*trap\s+-", s, re.M) is None,
        "EXIT/INT/TERM trap 에 restore 가 걸려 있지 않거나 이후 해제됩니다",
    ),
)

failures = []
# 검사 수는 입력에 의존하지 않는다 — 파일마다 같은 검사를 전부 돌린다.
# (예전 판은 -delete 유무로 개수가 달라져 79/78 을 오갔고, 그래서 개수가 계약의
#  일부인지 우연인지 알 수 없었다.)
checked = len(targets) * (len(CHECKS) + 1)
for path in targets:
    source = path.read_text(encoding="utf-8")
    for name, predicate, message in CHECKS:
        if not predicate(source):
            failures.append(f"FAIL {path}: [{name}] {message}")

    # 삭제 한정 보강: -delete 가 있으면 반드시 이 실행의 t0 로 한정한다.
    if "-delete" in source and 'newermt "@$RUN_T0"' not in source:
        failures.append(
            f"FAIL {path}: [삭제 한정] -delete 가 있는데 -newermt \"@$RUN_T0\" 로 한정하지 않습니다"
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
        f"probe safety source contract: {len(targets)} 파일, {checked} 검사, "
        f"{len(failures)} 실패 -> FAILED"
    )

print(
    f"probe safety source contract: {len(targets)} 파일, {checked} 검사, 0 실패 -> PASSED"
)
PY
