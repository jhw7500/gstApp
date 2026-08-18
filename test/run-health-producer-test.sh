#!/bin/bash
# camera health v1 producer 계약 시험.
#
# healthProducer.cpp 의 실제 publish() 가 낸 문서를 pim-package 의 실제 소비자
# (camera_healthd.py) 에 그대로 먹인다. 손으로 만든 fixture 로는 producer 와
# aggregator 사이의 계약 위반이 드러나지 않는다 - 기동 구간에 STARTING 관측을
# 내면서 최상위 status 를 "OK" 로 적던 판이 정확히 그렇게 통과했었다.
#
# 대상 바이너리는 aarch64 다. 개발 호스트에서는 qemu-aarch64 로 돌린다.
#
# 환경변수:
#   PIM_PACKAGE_DIR  pim-package-jhw 체크아웃 경로 (기본: ../pim-package-jhw)
#   SDK_LOC          Yocto SDK 경로 (기본: make-for-imx8 과 동일)
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(dirname "$HERE")
PIM_PACKAGE_DIR=${PIM_PACKAGE_DIR:-$(dirname "$ROOT")/pim-package-jhw}
SDK_LOC=${SDK_LOC:-/shared/fsl-imx-xwayland/5.10-hardknott}
SDK_NAME=${SDK_NAME:-cortexa53-crypto-poky-linux}
SYSROOT="$SDK_LOC/sysroots/$SDK_NAME"

HEALTHD="$PIM_PACKAGE_DIR/dist/pim/opt/pim/bin/camera_healthd.py"
REGISTRY="$PIM_PACKAGE_DIR/dist/pim/opt/pim/config/camera_health_error_codes_v1.json"

passed=0
failed=0
check() {
    if [ "$1" = "$2" ]; then
        passed=$((passed + 1))
        echo "  OK   $3"
    else
        failed=$((failed + 1))
        echo "  FAIL $3 (기대 '$2' / 실제 '$1')" >&2
    fi
}

echo "=== gstApp camera health producer ==="

# 건너뛰지 않는다. 소비자를 못 찾으면 실패다 - 조용히 통과하면 이 시험이
# 메우려는 공백이 그대로 재현된다.
for path in "$HEALTHD" "$REGISTRY"; do
    if [ ! -r "$path" ]; then
        echo "  FAIL 소비자를 찾을 수 없다: $path" >&2
        echo "       PIM_PACKAGE_DIR 을 pim-package-jhw 체크아웃으로 지정하라." >&2
        exit 1
    fi
done

RUNNER=()
if [ "$(uname -m)" != "aarch64" ]; then
    if ! command -v qemu-aarch64 >/dev/null; then
        echo "  FAIL aarch64 가 아닌 호스트인데 qemu-aarch64 가 없다" >&2
        exit 1
    fi
    RUNNER=(qemu-aarch64 -L "$SYSROOT")
fi

BIN="$ROOT/bin/testHealthProducer"
if [ ! -x "$BIN" ]; then
    echo "  FAIL $BIN 이 없다. './make-for-imx8 bin/testHealthProducer' 를 먼저 실행하라." >&2
    exit 1
fi

WORK=$(mktemp -d -t gstapp-health.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

DOCS="$WORK/docs"
mkdir -p "$DOCS"
if ! "${RUNNER[@]}" "$BIN" "$DOCS" >"$WORK/producer.log" 2>&1; then
    echo "  FAIL producer 하니스 실행 실패" >&2
    sed 's/^/       /' "$WORK/producer.log" >&2
    exit 1
fi

# 시나리오별 기대 판정.
#   gstApp producer 는 어떤 상태에서도 OK/NONE 이어야 한다. 여기가 MALFORMED 나
#   STALE 이 되면 aggregator 가 문서 자체를 버린 것이고, 그러면 진짜 열화와
#   producer 결함을 구분할 수 없다.
#   overall_status 는 세 producer 를 모두 채운 상태의 값이다.
#   root_block 은 aggregator 가 지목한 근본 원인 블록이다. producer 가 모든
#   관측에 root_cause=false 를 박아 두면 여기가 비어, 운영자는 FAILED 만 보고
#   원인은 하나도 못 본다. 상류(max9296/pim-healthd)가 정상인 시나리오이므로
#   gstApp 의 실패가 근본 원인으로 남아야 한다.
SCENARIOS="starting:RECOVERING:- ok:HEALTHY:- disabled:HEALTHY:- stall:FAILED:gstreamer nogrow:FAILED:recording buserr:FAILED:gstreamer"

for entry in $SCENARIOS; do
    name=${entry%%:*}
    rest=${entry#*:}
    expect=${rest%%:*}
    expect_root=${rest##*:}
    doc="$DOCS/$name.json"
    if [ ! -r "$doc" ]; then
        failed=$((failed + 1))
        echo "  FAIL $name: producer 가 문서를 내지 않았다" >&2
        continue
    fi

    run="$WORK/run-$name"
    mkdir -p "$run"
    cp "$doc" "$run/gstApp.json"

    # 나머지 두 producer 를 정상 상태로 채운다. gstApp 만 두면 부재로 인한
    # PRODUCER_STALE 때문에 언제나 DEGRADED 라 gstApp 의 기여를 볼 수 없다.
    python3 - "$run" <<'PY'
import json, sys
from pathlib import Path

run = Path(sys.argv[1])
doc = json.loads((run / "gstApp.json").read_text(encoding="utf-8"))
boot_id = doc["boot_id"]
observed = doc["observed_monotonic_ms"]

def obs(block, scope_kind, scope_id, channels=None):
    scope = {"kind": scope_kind, "id": scope_id}
    if channels is not None:
        scope["channels"] = channels
    return {
        "block": block, "scope": scope, "status": "OK", "code": "NONE",
        "count": 0,
        "evidence": [{"name": "harness", "source": "gstApp-test", "value": True}],
    }

def snapshot(producer, items):
    return {
        "schema": 1, "producer": producer, "boot_id": boot_id, "pid": 1,
        "sequence": 1, "observed_monotonic_ms": observed,
        "status": "OK", "observations": items,
    }

(run / "max9296.json").write_text(json.dumps(snapshot("max9296", [
    obs("sensor", "channel", "ch0", [0]),
    obs("isp", "channel", "ch0", [0]),
    obs("serializer", "channel", "ch0", [0]),
    obs("gmsl_link", "link", "phy-a", [0]),
    obs("deserializer", "global", "max9296-0"),
])), encoding="utf-8")

(run / "pim-probe.json").write_text(json.dumps(snapshot("pim-healthd", [
    obs("csi2", "csi", "csi0"),
    obs("capture", "pair", "csi0-dual", [0, 1]),
])), encoding="utf-8")

(run / "boot_id").write_text(boot_id, encoding="utf-8")
(run / "now_ms").write_text(str(observed + 100), encoding="utf-8")
PY

    now_ms=$(cat "$run/now_ms")
    if ! python3 "$HEALTHD" --once \
            --input-dir "$run" \
            --output "$run/aggregate.json" \
            --boot-id-file "$run/boot_id" \
            --registry "$REGISTRY" \
            --now-monotonic-ms "$now_ms" >"$run/healthd.log" 2>&1; then
        failed=$((failed + 1))
        echo "  FAIL $name: aggregator 실행 실패" >&2
        sed 's/^/       /' "$run/healthd.log" >&2
        continue
    fi

    read -r p_state p_code overall roots < <(python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
p = [x for x in d["producers"] if x["producer"] == "gstApp"][0]
blocks = sorted({r["block"] for r in d["root_causes"]})
print(p["state"], p["code"], d["overall_status"], ",".join(blocks) or "-")' "$run/aggregate.json")

    check "$p_state/$p_code" "OK/NONE" "$name: aggregator 가 문서를 받아들인다"
    check "$overall" "$expect" "$name: overall_status"
    check "$roots" "$expect_root" "$name: 근본 원인 블록"
done

echo
echo "gstApp health producer: $passed passed / $failed failed"
[ "$failed" -eq 0 ]
