#!/bin/bash
# cfgjson 단위테스트: make-for-imx8 크로스 빌드 → SHA 검증 배포 → 실행.
set -Eeuo pipefail
cd "$(dirname "$0")/.."

BOARD=${BOARD:-192.168.0.200}
BOARD_PW=${BOARD_PW:-root}
OUT=${OUT:-bin/testCfgjson}
REMOTE=${REMOTE:-/tmp/test_cfgjson.$$}
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o ConnectTimeout=8)
deployed=0

cleanup()
{
  local status=$?
  trap - EXIT
  if [ "$deployed" -eq 1 ]; then
    sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
      "rm -f '$REMOTE'" >/dev/null 2>&1 || status=120
  fi
  exit "$status"
}
trap cleanup EXIT

echo "[build] cross-compile (aarch64) ..."
./make-for-imx8 "$OUT"
LOCAL_SHA=$(sha256sum "$OUT" | cut -d' ' -f1)

echo "[deploy] -> ${BOARD}:${REMOTE}"
deployed=1
base64 "$OUT" | sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "base64 -d > '$REMOTE' && chmod +x '$REMOTE'"
REMOTE_SHA=$(sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "sha256sum '$REMOTE' | cut -d' ' -f1")
[ "$REMOTE_SHA" = "$LOCAL_SHA" ]

echo "[run] on board:"
sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" "$REMOTE"
