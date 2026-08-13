#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BOARD:=192.168.214.4}"
: "${BOARD_PW:?BOARD_PW must be set}"
: "${BIN:=bin/gstApp}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o ConnectTimeout=8)
REMOTE=/tmp/gstApp-sync-config-cli-test
base64 "$BIN" | sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "base64 -d > '$REMOTE' && chmod 755 '$REMOTE'"
help=$(sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "'$REMOTE' --help-all")
for option in \
  --rtsp-frame-id-sei --v4l2-sync-trace-sec \
  --channel-sync-trace-sec --rtsp-sync-trace-sec \
  --rtsp-test-stall-ch --rtsp-test-stall-after-sec \
  --rtsp-test-stall-duration-sec
do
  grep -q -- "$option" <<<"$help"
done
sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" "rm -f '$REMOTE'"
echo "sync config CLI contract: PASSED"
