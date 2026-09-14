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
# gstApp 이 읽는 병합 문서. 생산자 입력(/root/shared_v/edgeconf_pim.json)을 고쳐도
# 이 스크립트는 cam-operate 를 멈춘 채 돌아 반영되지 않는다 (이슈 #113).
# 두 층의 관계와 근거: docs/FPS_MEASUREMENT_SCENARIO.md §8.1
CONF=/run/pim-camera/config/pim_runtime.json
# 서비스가 멈추면 systemd 가 RuntimeDirectory 를 지우므로 쓰기 전마다 되살린다.
# 같은 디렉터리 임시 파일 + mv 로 발행한다(생산자 write_json_atomic 과 같은 원자성).
# install -d 가 아니라 mkdir -p -m 인 이유, 경로 형태와 파일 종류를 먼저 보는 이유는
# 같은 문서 §8.2. 이 정의는 13 벌이 바이트 동일해야 한다(게이트가 검사한다).
# shellcheck disable=SC2174  # 기본 CONF 기준이다 — RUNTIME_CONF 로 더 깊은 경로를 주면 중간 요소는 -m 을 못 받고 umask 를 따른다(docs §8.2 실측)
put_conf() { case $CONF in /*/*/*) ;; *) echo "!! CONF 가 /a/b/c 형태가 아니다: $CONF" >&2; return 1;; esac; if [ -L "$CONF" ] || { [ -e "$CONF" ] && [ ! -f "$CONF" ]; }; then echo "!! $CONF 가 정규 파일이 아니다 - 쓰지 않는다" >&2; return 1; fi; if mkdir -p -m 0750 "${CONF%/*/*}" "${CONF%/*}" && cp -f "$1" "$CONF.tmp.$$" && chmod 0640 "$CONF.tmp.$$" && mv -f "$CONF.tmp.$$" "$CONF"; then return 0; fi; rm -f "$CONF.tmp.$$"; echo "!! config 쓰기 실패: $1 -> $CONF" >&2; return 1; }
ORIG="$D/pim_runtime.json.orig"
ORIG_MD5="$D/pim_runtime.json.orig.md5"
HARDRESET=/opt/pim/bin/cam_hard_reset.sh
BIN="$D/gstApp.skew-test"
LOG="$D/run-$TAG.log"
RUNLOG="$D/runner.log"
APP_PID=""; MARKER="$D/.runstart"
# config 를 실제로 덮어쓰기 전에는 복원하지 않는다(중단이 정상 config 를 되돌리면 안 된다).
CONF_DIRTY=0

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
  if [ "$CONF_DIRTY" -eq 1 ]; then
    if [ -f "$ORIG" ] && [ -f "$ORIG_MD5" ]; then
      put_conf "$ORIG"; sync
      [ "$(md5of "$CONF")" = "$(cat "$ORIG_MD5")" ] \
        && log "config 복원 검증 OK ($(cat "$ORIG_MD5"))" \
        || log "!!! config 복원 md5 불일치 — 수동 확인 필요 !!!"
    else
      log "!!! 백업 부재 — config 복원 불가 !!!"
    fi
  else
    log "config 미투입 — 복원하지 않는다(발행 문서를 건드린 적이 없다)"
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

# 없는 채로 읽으면 md5 가 빈 문자열이 되어 표류 검사가 "live != backup" 이라는
# 엉뚱한 원인을 댄다. $CONF 의 수명은 같은 문서 §8.2 참조. 위 락과 같은 이유로
# trap '앞에' 둔다 — 아무것도 쓰지 않은 사전조건 실패가 restore 를 돌리면
# pkill·하드리셋·서비스 재기동이 이 실행이 건드린 적 없는 장비에 일어나고,
# restore 의 put_conf 가 없던 런타임 문서를 stale 내용으로 새로 만든다.
[ -e "$CONF" ] || { echo "병합 문서 없음: $CONF — cam-operate 를 먼저 기동할 것"; exit 2; }

# ── 1. 원본 백업 (최초 1회, 절대 덮지 않음) ──────────────────────────────────
if [ ! -f "$ORIG" ]; then
  [ "$(jq -r '.VHL_CAM.i2c1.ch2.enable' "$CONF")" = "false" ] || { log "!!! 배포 원본이 아닌 듯(ch2.enable!=false). 중단"; exit 3; }
  cp "$CONF" "$ORIG"; md5of "$ORIG" > "$ORIG_MD5"; log "원본 백업 생성 (md5 $(cat "$ORIG_MD5"))"
else
  log "기존 백업 사용 (md5 $(cat "$ORIG_MD5"))"
fi

# ── 2. 시험 config 생성 + 검증 ───────────────────────────────────────────────
TESTCONF="$D/pim_runtime.test.$TAG.json"   # 병합 문서 사본이므로 백업·원본과 같은 이름 규칙
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

# 여기까지는 $CONF 를 읽기만 하고 쓰는 곳은 전부 $D 안이라 보드 상태를 바꾸지 않는다.
# 그래서 trap 은 첫 파괴적 동작(systemctl stop) 바로 앞에 건다 — 위의 exit 3/4/5 는
# 아무것도 쓰지 않은 사전조건 실패인데, trap 이 그보다 위에 있으면 restore 가 돌아
# put_conf "$ORIG" 가 **서비스가 살아 있는 상태의 발행 문서**를 생산자 검증을 우회해
# 덮어쓰고 "복원 검증 OK" 까지 남긴다. cam-operate.service 의 ExecStartPost 는 그
# 문서가 validate 를 통과할 때까지 TimeoutStartSec=90s 를 돈다.
# 위치만으로는 닫히지 않는 창이 남는다 — trap 설치부터 아래 put_conf "$TESTCONF" 까지
# 사이에 INT/TERM 이 오면 restore 가 도는데, 그 구간 앞부분에서는 서비스가 아직 살아
# 있다. 그래서 복원은 CONF_DIRTY 로 조건부다(다른 측정 스크립트와 같은 방식).
# 서비스 재시작과 하드 리셋은 무조건 해도 무해하므로 그대로 둔다.
trap restore EXIT INT TERM

# ── 3. 정지 → 하드 리셋(epoch↑) → config 투입 → 기동 ────────────────────────
log "cam-operate 정지 (기동 전 epoch: $(epochs))"
systemctl stop cam-operate; sleep 3
pkill -x gstApp 2>/dev/null; pkill -x killcam 2>/dev/null; sleep 3
hard_reset "pre-run" || { log "!!! 하드 리셋 실패 — 중단"; exit 7; }

# 생산자가 아직 살아 있으면 쓰지 않는다 — 발행 문서를 생산자 검증 없이 덮고
# publish 와 경합한다. 종료코드로 판정하면 안 된다: 비활성도 조회 실패도 non-zero 라
# 구분되지 않아, 조회가 깨지면 생산자가 도는 중에도 그대로 쓴다(실측 systemd 249 —
# inactive rc=3, D-Bus 실패 rc=1 이고 후자는 stdout 이 빈다). 상태 문자열로 보고
# 모르는 상태·조회 실패에서는 중단한다(이슈 #113 PR 리뷰, Codex P1 ②).
CAM_STATE=$(systemctl is-active cam-operate.service 2>/dev/null); case $CAM_STATE in inactive|failed) ;; *) echo "!! cam-operate 상태가 [${CAM_STATE:-조회실패}] 다 - 시험 config 를 쓰지 않는다" >&2; exit 11;; esac
# cp 도중 죽어도 복원되도록 쓰기 "전"에 세운다
CONF_DIRTY=1
put_conf "$TESTCONF" || exit 11; sync; log "시험 config 투입"   # 11: 정지·하드리셋 후 쓰기 실패(2 는 보드 무변경)
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
# 부호도 받는다. 현재 코드에서 skew 는 rtMax-rtMin 이라 음수가 나올 수 없지만, 아래 분포
# 줄은 skew:[0-9-]*ms 로 음수를 잡으므로 여기서만 못 잡으면 두 출력이 어긋난다.
# 검증 도구가 특정 값 부류를 조용히 빠뜨리는 것은 이 스크립트의 목적과 정반대다.
log "  0 이 아닌 skew 표본: $(sed -E 's/\x1b\[[0-9;]*m//g; s/wall-skew:-?[0-9]+ms//g' "$LOG" | grep -aoE 'skew:-?[0-9]+ms' | grep -cvE 'skew:-?0ms') 건"
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
