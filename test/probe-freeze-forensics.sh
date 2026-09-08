#!/usr/bin/env bash
#
# probe-freeze-forensics.sh — "동결" 순간의 레지스터·커널로그를 포착한다.
#
# 대상 현상 (근인 `ae_on` 비대칭과는 **별개**, 미규명)
#   정상 fps 로 돌던 스트림이 얼마간 동작하다 **전 계층이 동시에 0** 이 되고 AP1302 센서
#   프레임시간 레지스터가 고정된다. 고정값에 104.x 와 **61.3(120 의 정확한 절반)** 이 반복된다.
#   발생 시각은 11 / 17 / 18 / 27 / 34 / 77 / 78 / 87 초로 넓게 흩어진다.
#   90 초 창 12 회: 통제 ch0+ch1 1/4, 통제 ch2+ch3 3/4. 정본 조건(exp=10000) 7 회는 0 건
#   (표본이 작아 상관은 추정). 앱 없는 독립 파이프라인에서도 발생한다 => 앱 문제가 아니다.
#
# 지금까지 없던 것
#   동결 시점의 **에러/트리거/링크 레지스터와 dmesg** 를 한 번도 안 봤다. 프로브가 HINF 와
#   R0x00FC 만 읽었기 때문이다. 이 스크립트는 그 공백을 메운다.
#
# 방법
#   - 동결이 잘 나는 조건을 일부러 쓴다: 통제 config(4채널 동일), exp=2000, ch2+ch3 @120.
#     (정본 조건은 동결이 안 나서 관측 기회가 없다.)
#   - 1 초 간격 계측 중 **CSI2 증분이 2 회 연속 0** 이면 즉시 레지스터 전체를 덤프한다.
#   - 비교 기준으로 스트림 안정 후(t≈25s) 같은 항목을 한 번 덤프해 둔다(BEFORE).
#   - `dmesg -w` 를 실행 내내 파일로 받아 동결 전후 커널 메시지를 타임스탬프와 함께 남긴다.
#   - 동결 후에도 끝까지 샘플링해 자연 복구 여부를 본다.
#
# 덤프 항목 (출처: max9296/docs/fps-limit-analysis.md §9)
#   AP1302 (듀얼 0x11/0x12): R0x0002 FRAME_CNT / R0x0006 ERROR / R0x00D8 SENSOR_LINE_TIME /
#     R0x00E0 SENSOR_FRAME_TIME / R0x00FC SENSOR_TOTAL_FRAME_TIME / R0x1000 CTRL /
#     R0x1184 ATOMIC / R0x1186 TRIGGER_CTRL / R0x2020 PREVIEW_MAX_FPS /
#     R0x5440 FLICK_CTRL / R0x6112 TRIGGER_MAX_MISMATCH
#   MAX9296 (0x48): 0x0013 CTRL3([3]=LOCKED) / 0x0313 CSI 출력 인에이블 / 0x0320 BACKTOP25
#   sysfs: prepare / link_status / health_raw
#
# 사용법: SAMPLES=90 TRIALS=6 ./probe-freeze-forensics.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-90}
TRIALS=${TRIALS:-6}
IVL_MS=${IVL_MS:-1000}
EXPCTL=${EXPCTL:-2000}
BPSCTL=${BPSCTL:-4096}
BEFORE_AT=${BEFORE_AT:-25000}   # 안정 구간 기준 덤프 시각(ms)
NPROC=$(nproc)

# ch2+ch3 듀얼와이드 (동결 관측률이 가장 높았던 조합)
BUS=1; DEV=1-0048; A_ADDR=0x11; B_ADDR=0x12; CSI_IRQ=32e40000.csi; ISI_IRQ=32e00000.isi

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/frz_$STAMP.log"
CSV="$OUT/frz_$STAMP.csv"
DUMP="$OUT/frz_${STAMP}_dumps.txt"
log() { echo "$*" | tee -a "$LOG"; }
dmp() { echo "$*" >>"$DUMP"; }

[ -e "$BACKUP" ] || { echo "백업 없음: $BACKUP"; exit 2; }
[ -x "$RESET" ] || { echo "리셋 스크립트 없음: $RESET"; exit 2; }
ORIG_MD5=$(md5sum "$BACKUP" | awk '{print $1}')
LIVE_MD5=$(md5sum "$CONF" | awk '{print $1}')
# config 를 실제로 덮어쓰기 전에는 복원하지 않는다(중단이 정상 config 를 되돌리면 안 된다).
CONF_DIRTY=0
# 이 실행이 시작된 시각. 녹화 삭제를 이 시점 이후 파일로만 한정한다
# (고정 10분 창은 직전 운영 녹화까지 지웠다).
RUN_T0=$(date +%s)
# 백업은 아무 스크립트도 갱신하지 않으므로, live 가 백업과 다르면 그 사이 운영 설정이 바뀐
# 것이다. 그대로 진행하면 복원이 그 변경을 조용히 되돌린다. 복원 후 md5 검사는 복사본을
# 자기 원본과 비교하는 것이라 이 위험을 구조적으로 탐지하지 못한다.
if [ "$LIVE_MD5" != "$ORIG_MD5" ]; then
	if [ "${ALLOW_CONF_DRIFT:-0}" = "1" ]; then
		echo "경고: live($LIVE_MD5) != 백업($ORIG_MD5) — ALLOW_CONF_DRIFT=1 로 진행합니다."
		echo "      복원 시 현재 운영 설정이 백업 시점으로 되돌아갑니다."
	else
		echo "중단: live config 가 백업과 다릅니다."
		echo "  live  =$LIVE_MD5  ($CONF)"
		echo "  backup=$ORIG_MD5  ($BACKUP)"
		echo "  백업을 갱신하거나, 되돌아가도 좋다면 ALLOW_CONF_DRIFT=1 로 다시 실행하세요."
		exit 2
	fi
fi
WAS=0
systemctl is-active --quiet cam-operate.service && WAS=1
ORIG_EN2=$(cat /sys/bus/i2c/devices/2-0048/enable 2>/dev/null || echo 0)
ORIG_EN1=$(cat /sys/bus/i2c/devices/1-0048/enable 2>/dev/null || echo 0)
DMESG_PID=""

kill_cap() {
	pkill -x capapp 2>/dev/null
	for _ in $(seq 1 15); do pgrep -x capapp >/dev/null || return 0; sleep 1; done
	pkill -9 -x capapp 2>/dev/null
	sleep 2
}

restore() {
	rc=$?
	[ -n "$DMESG_PID" ] && kill "$DMESG_PID" 2>/dev/null
	log ""
	log "### 복구"
	kill_cap
	if [ "$CONF_DIRTY" -eq 1 ]; then
		cp "$BACKUP" "$CONF"
		NOW=$(md5sum "$CONF" | awk '{print $1}')
		if [ "$NOW" = "$ORIG_MD5" ]; then
			log "  설정 복원 md5 일치 ($NOW)"
		else
			log "  !!! 설정 복원 md5 불일치: $NOW != $ORIG_MD5 — 수동 확인 필요 !!!"
		fi
	else
		log "  설정을 덮어쓴 적이 없어 복원을 생략합니다 (live 유지)"
	fi
	"$RESET" -q >>"$LOG" 2>&1
	sleep 2
	echo "$ORIG_EN2" >/sys/bus/i2c/devices/2-0048/enable 2>/dev/null
	echo "$ORIG_EN1" >/sys/bus/i2c/devices/1-0048/enable 2>/dev/null
	# 시작 시 멈춰 있었으면 기본적으로 그대로 두지만, 그 사실을 눈에 띄게 남긴다.
	# RESTORE_CAM_OPERATE=1 이면 시작 상태와 무관하게 되살린다(run-fps-scenario.sh 와 동일).
	if [ "$WAS" -eq 1 ] || [ "${RESTORE_CAM_OPERATE:-0}" = "1" ]; then
		systemctl start cam-operate.service >>"$LOG" 2>&1
		sleep 12
	else
		log "  !! cam-operate 를 정지 상태로 둡니다 (시작 시에도 정지 상태였음)."
		log "  !! 보드가 녹화하지 않습니다. 되살리려면 RESTORE_CAM_OPERATE=1 또는 수동 기동."
	fi
	log "  cam-operate: $(systemctl is-active cam-operate.service)"
	log "### 로그: $LOG"
	log "### CSV: $CSV"
	log "### 덤프: $DUMP"
	exit $rc
}
trap restore EXIT INT TERM

ap_rd() { i2ctransfer -f -y -a "$1" "w2@$2" "$3" "$4" "r$5" 2>/dev/null | sed 's/0x//g' | tr -d ' '; }
h2d() { [ -n "${1:-}" ] && printf '%d' "$((16#$1))" 2>/dev/null || echo ""; }
now_ms() { date +%s%3N; }

irq_of() { awk -v d="$1" -v n="$NPROC" '$NF==d{s=0;for(i=2;i<=n+1;i++)s+=$i;print s+0;exit}' /proc/interrupts; }

# 응답하는 첫 주소. 펌웨어 로드 후에 불러야 한다.
pick_addr() {
	for a in "$2" 0x3c; do
		[ -n "$(ap_rd "$1" "$a" 0x00 0x02 2)" ] && { echo "$a"; return 0; }
	done
	echo ""
}

# AP1302 한 채널의 관심 레지스터 전부 (이름=값 나열)
ap_dump() { # $1=bus $2=addr
	_b=$1; _a=$2
	printf '    FRAME_CNT(0002)=%s ERROR(0006)=%s LINE_TIME(00D8)=%s FRAME_TIME(00E0)=%s\n' \
		"$(ap_rd "$_b" "$_a" 0x00 0x02 2)" "$(ap_rd "$_b" "$_a" 0x00 0x06 2)" \
		"$(ap_rd "$_b" "$_a" 0x00 0xd8 4)" "$(ap_rd "$_b" "$_a" 0x00 0xe0 4)"
	printf '    TOTAL_FRAME_TIME(00FC)=%s CTRL(1000)=%s ATOMIC(1184)=%s TRIGGER_CTRL(1186)=%s\n' \
		"$(ap_rd "$_b" "$_a" 0x00 0xfc 4)" "$(ap_rd "$_b" "$_a" 0x10 0x00 2)" \
		"$(ap_rd "$_b" "$_a" 0x11 0x84 2)" "$(ap_rd "$_b" "$_a" 0x11 0x86 2)"
	printf '    PREVIEW_MAX_FPS(2020)=%s FLICK_CTRL(5440)=%s TRIG_MAX_MISMATCH(6112)=%s\n' \
		"$(ap_rd "$_b" "$_a" 0x20 0x20 2)" "$(ap_rd "$_b" "$_a" 0x54 0x40 2)" \
		"$(ap_rd "$_b" "$_a" 0x61 0x12 2)"
}

# MAX9296 디시리얼라이저 (0x48)
max_dump() { # $1=bus
	printf '    CTRL3(0013)=%s[LOCKED=%s] CSI_EN(0313)=%s BACKTOP25(0320)=%s\n' \
		"$(ap_rd "$1" 0x48 0x00 0x13 1)" \
		"$(v=$(h2d "$(ap_rd "$1" 0x48 0x00 0x13 1)"); [ -n "$v" ] && echo $(( (v >> 3) & 1 )) || echo '?')" \
		"$(ap_rd "$1" 0x48 0x03 0x13 1)" "$(ap_rd "$1" 0x48 0x03 0x20 1)"
}

dump_all() { # $1=라벨 $2=trial $3=시각ms
	dmp ""
	dmp "================ $1  trial=$2  t=$3 ms  $(date -Is) ================"
	dmp "  [AP1302 chA $A_ADDR]"; ap_dump "$BUS" "$AA" >>"$DUMP"
	dmp "  [AP1302 chB $B_ADDR]"; ap_dump "$BUS" "$AB" >>"$DUMP"
	dmp "  [MAX9296 bus$BUS]";    max_dump "$BUS" >>"$DUMP"
	dmp "  [sysfs]"
	dmp "    prepare: $(cat "/sys/bus/i2c/devices/$DEV/prepare" 2>/dev/null)"
	dmp "    link_status: $(cat "/sys/bus/i2c/devices/$DEV/link_status" 2>/dev/null)"
	dmp "    health_raw: $(cat "/sys/bus/i2c/devices/$DEV/health_raw" 2>/dev/null)"
	dmp "  [IRQ] csi=$(irq_of "$CSI_IRQ") isi=$(irq_of "$ISI_IRQ")"
}

# --- 준비 -------------------------------------------------------------------
if systemctl is-active --quiet cam-operate.service; then
	systemctl stop cam-operate.service >>"$LOG" 2>&1
	sleep 5
fi
pkill -x killcam 2>/dev/null
sleep 2
# 스테이징 실패를 조용히 넘기면 이전 회차의 낡은 바이너리가 측정된다.
cp /usr/local/bin/gstApp "$BIN" || { echo "중단: 앱 스테이징 복사 실패"; exit 2; }
chmod +x "$BIN" || { echo "중단: 앱 스테이징 chmod 실패"; exit 2; }

log "=== 동결 포렌식 $(date -Is) ==="
log "조건: 통제 config(4채널 동일), exp=$EXPCTL, bps=$BPSCTL, ch2+ch3 듀얼와이드 640x360@120."
log "창 ${SAMPLES}s x ${TRIALS}회. CSI2 증분이 2회 연속 0 이면 즉시 레지스터 덤프."
log ""
echo "trial,t_ms,csi_d,isi_d,a_dhinf,a_sfps,b_dhinf,b_sfps,state" >"$CSV"
: >"$DUMP"

for TR in $(seq 1 "$TRIALS"); do
	log "==============================================================="
	log "### 시도 $TR / $TRIALS"
	log "==============================================================="
	kill_cap
	if systemctl is-active --quiet cam-operate.service; then
		systemctl stop cam-operate.service >>"$LOG" 2>&1; sleep 5
	fi

	jq --argjson e "$EXPCTL" --argjson b "$BPSCTL" '
	      def ctl:
	          .vflip=false | .hflip=false | .ae_on=false | .ae_gain=256
	        | .bps=[$b,$b] | .awb="auto"
	        | .led_flash.enable=false | .led_flash.wiper=32 | .led_flash.flash_delay=0
	        | .gop=[30,15] | .profile=[0,0] | .quant=[-1,-1]
	        | .qp_min=[0,0] | .qp_max=[0,0] | .dz_x=32768 | .dz_y=32768;
	      .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=120
	    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
	    | .VHL_CAM.i2c2.exp_time=$e | .VHL_CAM.i2c1.exp_time=$e
	    | .VHL_CAM.i2c2.dz=100 | .VHL_CAM.i2c1.dz=100
	    | .VHL_CAM.i2c2.crop_enable=false | .VHL_CAM.i2c1.crop_enable=false
	    | .VHL_CAM.i2c2.ch0 |= ctl | .VHL_CAM.i2c2.ch1 |= ctl
	    | .VHL_CAM.i2c1.ch2 |= ctl | .VHL_CAM.i2c1.ch3 |= ctl
	    | .VHL_CAM.i2c2.ch0.enable=false | .VHL_CAM.i2c2.ch1.enable=false
	    | .VHL_CAM.i2c1.ch2.enable=true  | .VHL_CAM.i2c1.ch3.enable=true
	    ' "$BACKUP" >"$OUT/.fz2.json" || exit 1
	# cp 도중 죽어도 복원되도록 쓰기 "전"에 세운다
	CONF_DIRTY=1
	cp "$OUT/.fz2.json" "$CONF"
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	[ "$UNIQ" = "1" ] || log "  !!! 통제 검증 실패 (unique=$UNIQ) — 이 회차 무효 !!!"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	# 커널 메시지를 실행 내내 받는다 (동결 전후 타임스탬프 확보)
	dmesg -C 2>/dev/null
	DK="$OUT/frz_${STAMP}_dmesg_t${TR}.txt"
	dmesg -w >"$DK" 2>/dev/null &
	DMESG_PID=$!

	AL="$OUT/frz_${STAMP}_app_t${TR}.log"
	PC=$(irq_of "$CSI_IRQ"); PI=$(irq_of "$ISI_IRQ")
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	AA=""; AB=""; PHA=""; PHB=""
	ZERO=0; FROZE=0; DIDBEFORE=0; STARTED=0
	log "  t_ms     csi_d  isi_d   A_dHINF  A_sfps    B_dHINF  B_sfps   state"

	for i in $(seq 1 "$SAMPLES"); do
		TS=$(( $(now_ms) - T0 ))
		CC=$(irq_of "$CSI_IRQ"); CI=$(irq_of "$ISI_IRQ")
		DC=$(( CC - PC )); DI=$(( CI - PI )); PC=$CC; PI=$CI

		# 주소 판별은 펌웨어 로드 후에
		if [ -z "$AA" ] && [ "$TS" -ge 8000 ]; then
			AA=$(pick_addr "$BUS" "$A_ADDR"); AB=$(pick_addr "$BUS" "$B_ADDR")
		fi
		DA="-"; SA="-"; DB="-"; SB="-"
		if [ -n "$AA" ]; then
			CA=$(h2d "$(ap_rd "$BUS" "$AA" 0x00 0x02 2 | cut -c1-2)")
			TA=$(h2d "$(ap_rd "$BUS" "$AA" 0x00 0xfc 4)")
			SA=$(awk -v v="${TA:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print "-"}')
			[ -n "$PHA" ] && [ -n "$CA" ] && DA=$(( (CA - PHA + 256) % 256 ))
			[ -n "$CA" ] && PHA=$CA
		fi
		if [ -n "$AB" ]; then
			CB=$(h2d "$(ap_rd "$BUS" "$AB" 0x00 0x02 2 | cut -c1-2)")
			TB=$(h2d "$(ap_rd "$BUS" "$AB" 0x00 0xfc 4)")
			SB=$(awk -v v="${TB:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print "-"}')
			[ -n "$PHB" ] && [ -n "$CB" ] && DB=$(( (CB - PHB + 256) % 256 ))
			[ -n "$CB" ] && PHB=$CB
		fi

		STATE="-"
		[ "$DC" -gt 0 ] && STARTED=1

		# 안정 구간 기준 덤프
		if [ "$STARTED" -eq 1 ] && [ "$DIDBEFORE" -eq 0 ] && [ "$TS" -ge "$BEFORE_AT" ]; then
			dump_all "BEFORE(정상)" "$TR" "$TS"; DIDBEFORE=1; STATE="BEFORE덤프"
			log "    -> BEFORE 덤프 (t=${TS}ms)"
		fi

		# 동결 감지: 스트림이 한번 흐른 뒤 CSI2 증분 0 이 2회 연속
		if [ "$STARTED" -eq 1 ] && [ "$FROZE" -eq 0 ]; then
			if [ "$DC" -eq 0 ]; then ZERO=$(( ZERO + 1 )); else ZERO=0; fi
			if [ "$ZERO" -ge 2 ]; then
				FROZE=1; STATE="**동결**"
				log "    -> 동결 감지 t=${TS}ms — 레지스터 덤프"
				dump_all "FREEZE(동결직후)" "$TR" "$TS"
				dmp "  [dmesg 마지막 25줄]"
				tail -25 "$DK" 2>/dev/null | sed 's/^/    /' >>"$DUMP"
			fi
		fi

		printf "  %-8s %6s %6s   %7s %7s    %7s %7s   %s\n" \
			"$TS" "$DC" "$DI" "$DA" "$SA" "$DB" "$SB" "$STATE" | tee -a "$LOG"
		echo "$TR,$TS,$DC,$DI,$DA,$SA,$DB,$SB,$STATE" >>"$CSV"

		NEXT=$(( T0 + i * IVL_MS )); SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done

	# 동결 후 상태(회복 여부) 한 번 더
	if [ "$FROZE" -eq 1 ]; then
		dump_all "AFTER(창 종료시)" "$TR" "$(( $(now_ms) - T0 ))"
		log "  결과: 동결 발생"
	elif [ "$STARTED" -eq 1 ]; then
		log "  결과: 완주 (동결 없음)"
	else
		log "  결과: 스트림 시작 안 됨"
	fi
	log "  앱 enc-stat in>0: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0)줄"
	log "  드라이버 경고/에러: $(grep -icE "max9296.*(warn|err|fail|stale|timeout)" "$DK" 2>/dev/null || echo 0)건"
	log ""

	kill "$DMESG_PID" 2>/dev/null; DMESG_PID=""
	kill_cap
	find /dev/shm -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
	sleep 2
done

log "=== 종료 $(date -Is) ==="
log "동결 회차: $(grep -c '\*\*동결\*\*' "$CSV" || echo 0) / $TRIALS"
