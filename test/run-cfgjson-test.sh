#!/bin/bash
# cfgjson 단위테스트: aarch64 크로스컴파일 → 보드 전송(base64) → 실행.
# 의존: SDK(/shared/fsl-imx-xwayland), sshpass. 보드 런타임: libjson-c, libglib (gstApp가 사용 → 존재).
set -eo pipefail
cd "$(dirname "$0")/.."

SDK_LOC=${SDK_LOC:-/shared/fsl-imx-xwayland/5.10-hardknott}
SDK_NAME=${SDK_NAME:-cortexa53-crypto-poky-linux}
BOARD=${BOARD:-192.168.0.200}
BOARD_PW=${BOARD_PW:-root}
OUT=${OUT:-/tmp/test_cfgjson}

[ -e "${SDK_LOC}/environment-setup-${SDK_NAME}" ] || {
  echo "SDK env 없음: ${SDK_LOC}/environment-setup-${SDK_NAME}"; exit 1; }
# shellcheck disable=SC1090
. "${SDK_LOC}/environment-setup-${SDK_NAME}"

echo "[build] cross-compile (aarch64) ..."
# shellcheck disable=SC2086
$CXX cfgjson.cpp test/test_cfgjson.cpp -o "$OUT" \
  $(pkg-config --cflags --libs json-c glib-2.0)

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"
echo "[deploy] -> ${BOARD}:/tmp/test_cfgjson"
base64 "$OUT" | sshpass -p "$BOARD_PW" ssh $SSH_OPTS root@"$BOARD" \
  "base64 -d > /tmp/test_cfgjson && chmod +x /tmp/test_cfgjson"

echo "[run] on board:"
sshpass -p "$BOARD_PW" ssh $SSH_OPTS root@"$BOARD" "/tmp/test_cfgjson"
