#!/bin/bash
# 측정 스크립트가 병합 문서(/run/pim-camera/config/pim_runtime.json)를 직접 고쳐도
# 생산자 입력(edgeconf)을 고친 뒤 publish 한 것과 결과가 같은지 확인한다 (이슈 #113).
#
# 이 등식이 이슈 #113 수정의 근거다. 성립하는 이유는 camera_runtime_config.py 가
# VHL_CAM 을 그대로 복사하기 때문이고, 측정 스크립트의 jq 프로그램은 전부
# .VHL_CAM.* 아래만 건드린다. 등식이 깨지면 스크립트가 의도와 다른 것을 측정하게
# 되므로 여기서 잡는다. 손으로 만든 fixture 가 아니라 **실제 생산자 모듈과 실제
# 배포 fixture** 를 쓴다 — 사본으로는 생산자 쪽 변경이 드러나지 않는다.
#
# 건너뛰지 않는다. 생산자를 못 찾으면 실패다 - 조용히 통과하면 이 시험이 메우려는
# 공백이 그대로 재현된다 (test/run-health-producer-test.sh 와 같은 정책).
#
# 환경변수:
#   PIM_PACKAGE_DIR  pim-package-jhw 체크아웃 경로 (기본: ../pim-package-jhw)
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(dirname "$HERE")
PIM_PACKAGE_DIR=${PIM_PACKAGE_DIR:-$(dirname "$ROOT")/pim-package-jhw}

PIM="$PIM_PACKAGE_DIR/dist/pim/opt/pim"
HELPER="$PIM/bin/camera_runtime_config.py"
EDGE="$PIM/config/edgeconf_pim_base.json"
ORD="$PIM/config/ord_vcm_conf.json"

echo "=== gstApp runtime config commute ==="

for path in "$HELPER" "$EDGE" "$ORD"; do
	if [ ! -r "$path" ]; then
		echo "  FAIL 생산자를 찾을 수 없다: $path" >&2
		echo "       PIM_PACKAGE_DIR 을 pim-package-jhw 체크아웃으로 지정하라." >&2
		echo "runtime config commute check: FAILED"
		exit 1
	fi
done
if ! command -v jq >/dev/null; then
	echo "  FAIL jq 가 없다 - 측정 스크립트가 쓰는 편집기라 대체하지 않는다" >&2
	echo "runtime config commute check: FAILED"
	exit 1
fi

# 파이썬이 os._exit 처럼 잡을 수 없는 원시로 끝나면 판정 줄이 아예 없을 수 있다.
# 판정 줄 개수를 셸에서 확인해, 하나가 아니면 그 자체를 실패로 다룬다.
PYOUT=$(python3 - "$(readlink -f "$HELPER")" "$(readlink -f "$EDGE")" "$(readlink -f "$ORD")" <<'PY'
import sys, json, shutil, tempfile, pathlib, subprocess, importlib.util

helper, edge_src, ord_src = sys.argv[1], sys.argv[2], sys.argv[3]

# 측정 스크립트들이 실제로 쓰는 편집 형태 (전부 .VHL_CAM.* 하위).
# 주의: 이 문자열은 손으로 적은 것이고 실제 스크립트와 연동되지 않는다. 스크립트의
# jq 가 .VHL_CAM 밖으로 나가지 않는다는 전제는 **어디서도 검사되지 않는다**(docs §8.3).
JQ = ('.VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=120'
      ' | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1'
      ' | .VHL_CAM.i2c2.exp_time=2000 | .VHL_CAM.i2c1.exp_time=2000'
      ' | .VHL_CAM.i2c2.crop_enable=false | .VHL_CAM.i2c1.crop_enable=false'
      ' | .VHL_CAM.i2c2.ch0.enable=false | .VHL_CAM.i2c2.ch1.enable=false'
      ' | .VHL_CAM.i2c1.ch2.enable=true  | .VHL_CAM.i2c1.ch3.enable=true')


def jq(doc):
    p = subprocess.run(["jq", "-S", "-c", JQ], input=json.dumps(doc),
                       capture_output=True, text=True)
    if p.returncode != 0:
        # SystemExit 을 쓰면 안 된다 - BaseException 하위라 아래 except Exception 이
        # 잡지 못하고, fails 가 0 인 채 finally 가 PASSED 를 찍는다(실측 2026-09-14).
        raise RuntimeError("jq 실패: %s" % p.stderr.strip())
    return json.loads(p.stdout)


fails = 0
root = None
# 생산자 모듈 적재까지 **전부** 이 안에 둔다. 적재를 밖에 두었더니 import 중 종료에서
# 판정 줄이 한 줄도 남지 않았다(실측 2026-09-14). 판정 줄이 없으면 보는 쪽이 성공으로
# 읽는다 - 이 시험이 없애려는 것이 바로 그 조용한 거짓이다.
try:
    spec = importlib.util.spec_from_file_location("crc", helper)
    crc = importlib.util.module_from_spec(spec)
    sys.modules["crc"] = crc      # 생산자의 dataclass 가 sys.modules 를 참조한다
    spec.loader.exec_module(crc)

    # 하네스 불일치를 설정 판정으로 오보하지 않는다.
    missing = [n for n in ("merge_source_documents", "validate_runtime")
               if not hasattr(crc, n)]
    if missing:
        raise RuntimeError("생산자가 %s 를 export 하지 않는다 - 이 시험의 하네스가"
                           " 생산자와 어긋났다(설정 문제가 아니다)" % ", ".join(missing))

    root = pathlib.Path(tempfile.mkdtemp(prefix="commute-"))
    shutil.copy(edge_src, root / "edgeconf_pim.json")
    shutil.copy(ord_src, root / "ord_vcm_conf.json")
    edge = json.loads((root / "edgeconf_pim.json").read_text())

    # A: 병합 문서를 직접 편집 (측정 스크립트가 지금 하는 일)
    after = jq(crc.merge_source_documents(root).document)

    # B: 입력을 편집한 뒤 생산자가 병합 (서비스가 살아 있을 때 일어나는 일)
    (root / "edgeconf_pim.json").write_text(json.dumps(jq(edge)))
    before = crc.merge_source_documents(root).document

    if json.dumps(after, sort_keys=True) != json.dumps(before, sort_keys=True):
        print("FAIL: 병합 문서 편집과 입력 편집+publish 의 결과가 다릅니다")
        fails += 1

    for name in ("VHL_CAM", "ORD", "VCM"):
        if not isinstance(after.get(name), dict):
            print("FAIL: 편집 결과에 %s 가 없습니다 (gstApp 이 기동에 실패한다)" % name)
            fails += 1

    vhl = after.get("VHL_CAM", {})
    if (vhl.get("fps"), vhl.get("cam_width")) != (120, 640):
        print("FAIL: 편집이 반영되지 않았습니다 fps=%r width=%r"
              % (vhl.get("fps"), vhl.get("cam_width")))
        fails += 1

    try:
        crc.validate_runtime(after)
    except Exception as exc:                                  # noqa: BLE001
        print("FAIL: 편집 결과가 생산자 검증을 통과하지 못합니다: %s: %s"
              % (type(exc).__name__, exc))
        fails += 1
except Exception as exc:                                      # noqa: BLE001
    # fails 를 먼저 올린다 - 예외의 __str__ 이 던지면 print 가 실패하는데, 그때
    # fails 가 0 이면 finally 가 PASSED 를 찍은 채 프로세스는 1 로 끝난다.
    fails += 1
    print("FAIL: 검사 도중 예외: %s: %s" % (type(exc).__name__, exc))
except BaseException as exc:                                  # noqa: BLE001
    # SystemExit·KeyboardInterrupt 는 다시 던지지 않는다. SystemExit(0) 을 그대로
    # 던지면 stdout 은 FAILED 인데 종료코드가 0 이 되어 판정과 어긋난다.
    fails += 1
    print("FAIL: 검사가 비정상 종료했습니다: %s" % type(exc).__name__)
finally:
    if root is not None:
        shutil.rmtree(root, ignore_errors=True)
    print("runtime config commute check: %s" % ("FAILED" if fails else "PASSED"))

raise SystemExit(1 if fails else 0)
PY
)
PYRC=$?
printf '%s\n' "$PYOUT"
VERDICTS=$(printf '%s\n' "$PYOUT" | grep -c '^runtime config commute check: ')
if [ "$VERDICTS" -ne 1 ]; then
	echo "runtime config commute check: FAILED"
	echo "  !! 판정 줄이 ${VERDICTS} 개다 - 검사가 비정상 종료했다" >&2
	exit 1
fi
exit "$PYRC"
