#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BOARD:=192.168.214.4}"
: "${BOARD_PW:?BOARD_PW must be set}"
: "${BIN:=bin/gstApp}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o ConnectTimeout=8)
REMOTE=/tmp/gstApp-sync-config-cli-test.$$
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

LOCAL_SHA=$(sha256sum "$BIN" | cut -d' ' -f1)
deployed=1
base64 "$BIN" | sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "base64 -d > '$REMOTE' && chmod 755 '$REMOTE'"
REMOTE_SHA=$(sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "sha256sum '$REMOTE' | cut -d' ' -f1")
[ "$REMOTE_SHA" = "$LOCAL_SHA" ]
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
echo "sync config CLI contract: PASSED"
