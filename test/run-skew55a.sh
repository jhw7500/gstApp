#!/bin/bash
# gstApp #55a — 4채널 분할경계 '절대' skew 측정  (v2: 하드웨어 epoch 리셋 포함)
#   usage: run-skew55a.sh <W> <H> <FPS> [MINUTES]
#
# v1 실패 교훈: max9296 드라이버는 한 하드웨어 epoch 안에서 prepare fingerprint
#   (해상도·fps·enable) 변경을 -ESTALE 로 거부한다 (max9296.c:4795-4801, 5079-5087).
#   epoch 은 실제 전원 전환(:2134) 또는 마지막 power user 해제(:6701) 시에만 오른다.
#   → 시험 전에도, 복원 시에도 cam_hard_reset.sh 로 epoch 을 올려야 한다.
#     (복원 시 필수: 시험이 성공해 @FPS 로 프로그래밍되면 운영 @30 재기동도 ESTALE 이 된다)
set -u

W="${1:?width}"; H="${2:?height}"; FPS="${3:?fps}"; MIN="${4:-15}"
TAG="${W}x${H}@${FPS}"
D=/root/skew55a
CONF=/root/shared_v/edgeconf_pim.json
ORIG="$D/edgeconf_pim.json.orig"
ORIG_MD5="$D/edgeconf_pim.json.orig.md5"
HARDRESET=/opt/pim/bin/cam_hard_reset.sh
BIN="$D/gstApp.skew-test"
LOG="$D/run-$TAG.log"
RUNLOG="$D/runner.log"
APP_PID=""; MARKER="$D/.runstart"

mkdir -p "$D"
log(){ printf '[%s] %s\n' "$(date -Is)" "$*" | tee -a "$RUNLOG"; }
md5of(){ md5sum < "$1" | cut -d' ' -f1; }
isi(){ awk '$1 ~ /^[0-9]+:$/ && tolower($NF) ~ /\.isi$/ {
         for (i=2; i<=NF; i++) { if ($i ~ /^[0-9]+$/) s+=$i; else break }
       } END { printf "%d\n", s+0 }' /proc/interrupts; }
prep(){ cat "/sys/bus/i2c/devices/$1/prepare" 2>/dev/null; }
epochs(){ echo "CSI0[$(prep 2-0048 | grep -o 'epoch=[0-9]*')] CSI1[$(prep 1-0048 | grep -o 'epoch=[0-9]*')]"; }
links(){ echo "CSI0=$(cat /sys/bus/i2c/devices/2-0048/link_status 2>&1) CSI1=$(cat /sys/bus/i2c/devices/1-0048/link_status 2>&1)"; }

hard_reset(){                       # $1 = 라벨
  log "  hard reset ($1) — epoch 전: $(epochs)"
  "$HARDRESET" -q; local rc=$?
  log "  hard reset rc=$rc  epoch 후: $(epochs)  link: $(links)  video노드: $(ls -1 /dev/video* 2>/dev/null | wc -l)"
  [ "$rc" -eq 2 ] && log "  !!! rc=2 = 모듈 refcnt 음수 — 재부팅 필요 !!!"
  return $rc
}

restore() {
  local rc=$?
  log "=== restore (rc=$rc) ==="
  if [ -n "$APP_PID" ] && kill -0 "$APP_PID" 2>/dev/null; then
    kill -TERM "$APP_PID" 2>/dev/null
    for _ in $(seq 1 30); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 1; done
    kill -0 "$APP_PID" 2>/dev/null && { log "SIGKILL $APP_PID"; kill -KILL "$APP_PID" 2>/dev/null; }
  fi
  pkill -x gstApp 2>/dev/null; sleep 3
  if [ -f "$ORIG" ] && [ -f "$ORIG_MD5" ]; then
    cp -f "$ORIG" "$CONF"; sync
    [ "$(md5of "$CONF")" = "$(cat "$ORIG_MD5")" ] \
      && log "config 복원 검증 OK ($(cat "$ORIG_MD5"))" \
      || log "!!! config 복원 md5 불일치 — 수동 확인 필요 !!!"
  else
    log "!!! 백업 부재 — config 복원 불가 !!!"
  fi
  hard_reset "restore"              # 운영 지문(@원본)으로 되돌아갈 수 있게 epoch 을 올린다
  systemctl start cam-operate && log "cam-operate 재시작 요청"
  sleep 20
  log "복원 후: cam-operate=$(systemctl is-active cam-operate) gstApp=$(pgrep -x gstApp >/dev/null && echo up || echo DOWN) link:$(links)"
  log "  운영 prepare: $(prep 2-0048 | grep -oE 'state=[A-Z]+|width=[0-9]+|height=[0-9]+|fps=[0-9]+|errno=[-0-9]+' | tr '\n' ' ')"
  log "=== restore 완료 ==="
}
# 동시 실행 방지 — 두 러너가 겹치면 서로의 gstApp 을 pkill 하고 config/하드리셋이 엉킨다.
# trap 설치 '전에' 잡는다. 락 실패로 빠질 때 restore 가 돌면 남의 런을 망친다.
exec 9>"$D/.runner.lock"
if ! flock -n 9; then
  echo "[$(date -Is)] 다른 러너가 실행 중이다 — 중단 (락: $D/.runner.lock)" | tee -a "$RUNLOG"
  exit 9
fi

trap restore EXIT INT TERM

# ── 1. 원본 백업 (최초 1회, 절대 덮지 않음) ──────────────────────────────────
if [ ! -f "$ORIG" ]; then
  [ "$(jq -r '.VHL_CAM.i2c1.ch2.enable' "$CONF")" = "false" ] || { log "!!! 배포 원본이 아닌 듯(ch2.enable!=false). 중단"; exit 3; }
  cp "$CONF" "$ORIG"; md5of "$ORIG" > "$ORIG_MD5"; log "원본 백업 생성 (md5 $(cat "$ORIG_MD5"))"
else
  log "기존 백업 사용 (md5 $(cat "$ORIG_MD5"))"
fi

# ── 2. 시험 config 생성 + 검증 ───────────────────────────────────────────────
TESTCONF="$D/edgeconf.test.$TAG.json"
jq --argjson w "$W" --argjson h "$H" --argjson f "$FPS" '
    .VHL_CAM.cam_width = $w | .VHL_CAM.cam_height = $h | .VHL_CAM.fps = $f
  | .VHL_CAM.debug_level = 7
  | .VHL_CAM.i2c2.exp_time = 10000 | .VHL_CAM.i2c1.exp_time = 10000
  | ( .VHL_CAM.i2c2.ch0, .VHL_CAM.i2c2.ch1, .VHL_CAM.i2c1.ch2, .VHL_CAM.i2c1.ch3 ) |= (
        .enable = true | .ae_on = true | .ae_gain = 256 | .awb = "auto"
      | .hflip = false | .vflip = false | .dz_x = 32768 | .dz_y = 32768
      | .led_flash.enable = false )
' "$ORIG" > "$TESTCONF" || { log "jq 변환 실패"; exit 4; }
V=$(jq -r '[ .VHL_CAM.cam_width, .VHL_CAM.cam_height, .VHL_CAM.fps, .VHL_CAM.debug_level,
             .VHL_CAM.i2c2.exp_time, .VHL_CAM.i2c1.exp_time,
             ( [ .VHL_CAM.i2c2.ch0, .VHL_CAM.i2c2.ch1, .VHL_CAM.i2c1.ch2, .VHL_CAM.i2c1.ch3 ]
               | map( [ .enable, .ae_on, .ae_gain, .awb, .hflip, .vflip, .led_flash.enable ] | tostring )
               | unique | length ) ] | tostring' "$TESTCONF")
[ "$V" = "[$W,$H,$FPS,7,10000,10000,1]" ] || { log "시험 config 검증 실패: $V"; exit 5; }
log "시험 config 검증 OK: $V"

# ── 3. 정지 → 하드 리셋(epoch↑) → config 투입 → 기동 ────────────────────────
log "cam-operate 정지 (기동 전 epoch: $(epochs))"
systemctl stop cam-operate; sleep 3
pkill -x gstApp 2>/dev/null; pkill -x killcam 2>/dev/null; sleep 3
hard_reset "pre-run" || { log "!!! 하드 리셋 실패 — 중단"; exit 7; }

cp -f "$TESTCONF" "$CONF"; sync; log "시험 config 투입"
# 기본은 운영 바이너리. SKEW55A_SRC_BIN 으로 시험 빌드를 지정할 수 있다.
SRC_BIN="${SKEW55A_SRC_BIN:-/usr/local/bin/gstApp}"
[ -x "$SRC_BIN" ] || { log "시험 바이너리 없음: $SRC_BIN"; exit 8; }
cp -f "$SRC_BIN" "$BIN"; log "바이너리 $SRC_BIN md5 $(md5of "$BIN")"

touch "$MARKER"; ISI0=$(isi); T0=$(date +%s)
cd /root || { log "cd /root 실패"; exit 10; }
# SKEW55A_EXTRA_ARGS 로 추가 인자를 줄 수 있다(예: -X 100 으로 스냅백을 강제 유발).
# shellcheck disable=SC2086  # 인자 분리가 의도다
setsid "$BIN" -d 5 -m 4 -g 7 ${SKEW55A_EXTRA_ARGS:-} </dev/null >"$LOG" 2>&1 &
APP_PID=$!
log "기동 pid=$APP_PID log=$LOG"

# ── 4. go/no-go (150초) ──────────────────────────────────────────────────────
sleep 150
log "--- go/no-go ---"
log "  살아있나       : $(kill -0 "$APP_PID" 2>/dev/null && echo yes || echo NO)"
log "  해상도/fps     : $(grep -ao 'width:[0-9]*, height:[0-9]*, csi1_fps:[0-9]*, csi2_fps:[0-9]*' "$LOG" | head -1)"
log "  chEn / dbg     : $(grep -ao 'chEn:0x[0-9a-f]*' "$LOG" | head -1)  $(grep -ao 'dbgLevel:[0-9]*' "$LOG" | head -1)"
log "  LED off/on     : $(grep -ac 'led_flash: enable=0' "$LOG") / $(grep -ac 'led_flash: enable=1' "$LOG")"
log "  ISP 설정 종류  : $(grep -ao 'ae_on=[0-9] gain=[0-9]* exp_time=[0-9]* awb=[a-z0-9]*' "$LOG" | sort -u | wc -l) (1이어야 정상)"
log "  PREPARE errno  : $(grep -ao 'primary_errno=[-0-9]*' "$LOG" | sort -u | tr '\n' ' ')"
log "  link           : $(links)"
log "  DEBUG skew 줄  : $(grep -ac 'skew:' "$LOG")   NOTICE: $(grep -ao 'skew basis: .*' "$LOG" | head -1)"
log "  녹화 .part     : $(ls -1 /dev/shm/*.part 2>/dev/null | wc -l) 개 (4 기대)"
kill -0 "$APP_PID" 2>/dev/null || { log "!!! 기동 실패 — 중단"; exit 6; }

# ── 5. 본 측정 ───────────────────────────────────────────────────────────────
log "본 측정 ${MIN}분 시작"
for i in $(seq 1 "$MIN"); do
  sleep 60
  [ $((i % 5)) -eq 0 ] && log "  ${i}/${MIN}분 skew표본=$(grep -ac 'skew:' "$LOG") shm=$(df -h /dev/shm | awk 'NR==2{print $5}')"
  kill -0 "$APP_PID" 2>/dev/null || { log "!!! 프로세스 사망 (${i}분)"; break; }
done
ISI1=$(isi); T1=$(date +%s); EL=$((T1-T0)); DI=$((ISI1-ISI0))

# ── 6. 수집 ──────────────────────────────────────────────────────────────────
log "=== 결과 $TAG ==="
# ISI 는 CSI 당 '결합 프레임'(2채널이 한 프레임) 하나를 센다 → 기대값은 fps x CSI수(2)
log "  경과 ${EL}s  ISI 증가 ${DI}  → 실측 $(awk -v d=$DI -v e=$EL 'BEGIN{if(e>0)printf "%.2f",d/e; else print "?"}') /s (기대 fps x CSI2 = $((FPS*2)))"
log "  스냅백 발생: $(grep -ac "Snap-back" "$LOG") 회 / 그중 forced-rt 로그: $(grep -ac "forced split at running-time" "$LOG") 회"
# wall-skew: 를 먼저 지운다 — 안 그러면 'wall-skew:264ms' 가 'skew:264ms' 로 잡혀 오집계된다.
log "  0 이 아닌 skew 표본: $(sed -E 's/\x1b\[[0-9;]*m//g; s/wall-skew:[0-9]+ms//g' "$LOG" | grep -aoE 'skew:[0-9]+ms' | grep -cv 'skew:0ms') 건"
log "  skew 표본 분포:"
grep -ao 'skew:[0-9-]*ms[^,]*, wall-skew:[0-9-]*ms, active:[0-9]*' "$LOG" | sort | uniq -c | sort -rn | head -15 | sed 's/^/    /' | tee -a "$RUNLOG"
# 앱을 먼저 정상 종료해 마지막 조각까지 닫는다 (감시자 정지 중이라 .part 가 최종본이다)
if kill -0 "$APP_PID" 2>/dev/null; then
  log "  조각 마감을 위해 앱 정상 종료 (SIGTERM $APP_PID)"
  kill -TERM "$APP_PID" 2>/dev/null
  for _ in $(seq 1 30); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 1; done
  kill -0 "$APP_PID" 2>/dev/null && kill -KILL "$APP_PID" 2>/dev/null
  APP_PID=""; sleep 2
fi
# 복원(=cam-operate 재기동 시 /dev/shm 정리)이 지우기 전에 확보한다
FD="$D/files-$TAG"; rm -rf "$FD"; mkdir -p "$FD"; NF=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  cp -f "$f" "$FD/$(basename "$f" .part)" && NF=$((NF+1))
done < <(find /dev/shm -maxdepth 1 -name '*.mp4.part' -newer "$MARKER" 2>/dev/null | sort)
log "  시험 녹화 파일 확보: ${NF}개 -> $FD"
log "  파일별 프레임 수:"
mapfile -t F < <(find "$FD" -name '*.mp4' 2>/dev/null | sort)
[ "${#F[@]}" -gt 0 ] \
  && /root/run-record-sync-check.sh "$FPS" "${F[@]}" 2>&1 | tail -25 | sed 's/^/    /' | tee -a "$RUNLOG" \
  || log "    (확보된 파일 없음)"
cp -f "$LOG" "$D/collected-$TAG.log"
log "=== $TAG 측정 종료 — restore 로 이어짐 ==="
