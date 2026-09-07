#!/usr/bin/env bash
#
# probe-freeze-fastsample.sh — 동결의 **전환 순간**을 50Hz 로 포착한다.
#
# 확정된 것 (frz_20260907_103402 6회 + frzexp_20260907_105006 15회)
#   동결 = AP1302 총프레임시간(R0x00FC)이 8,3xx~8,4xx us 에서 **약 9,570us(104.5fps)로 점프**하는 것.
#   한쪽만 점프할 때도(9,560/8,351 · 9,581/8,303) 양쪽이 함께 갈 때도(9,574/9,567) 있다.
#   완주 13회차는 시종 8,3xx~8,4xx 에 머물고 채널 간 차이 2~68us — **표류가 전혀 없다.**
#   즉 점진 변화가 아니라 **계단 변화**다. 유효 프레임시간(R0x00E0=7,344us = 라인 6.8us x 1,080)은
#   항상 불변이고 늘어나는 2,226us 는 약 327 라인분의 블랭킹이다.
#   ERROR=0x0000, GMSL2 LOCKED=1, dmesg 무음, 트리거 설정 불변, 노출 의존성 미확인(1/5·1/5·0/5).
#
# 이 스크립트가 메우는 공백
#   기존 프로브는 1초 간격이라 8,400 -> 9,570 **전환 과정이 한 샘플 안에 숨는다.**
#   중간 상태(예: 계단이 여러 단인지, 한 프레임 만에 뛰는지)를 볼 수 없었다.
#   여기서는 20ms(50Hz) 로 읽는다 — 120fps 프레임당 약 2.4 샘플.
#
# 성능 근거 (타겟 실측)
#   i2ctransfer 한 번에 4쌍(양 채널 TOTAL+FRAME_CNT)을 묶어 읽으면 **3ms**.
#   /proc/interrupts awk 는 6ms 이므로 IRQ 는 5샘플마다(100ms) 읽는다.
#   시각은 bash 5 내장 EPOCHREALTIME 을 써서 date 프로세스를 없앤다.
#
# 조건: 통제 config(4채널 동일), exp=2000, ch2+ch3 듀얼와이드 640x360@120 — 동결 관측률 3/11.
#
# 사용법: SAMPLES_SEC=90 TRIALS=8 ./probe-freeze-fastsample.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES_SEC=${SAMPLES_SEC:-90}
TRIALS=${TRIALS:-8}
IVL_US=${IVL_US:-20000}          # 20ms = 50Hz
IRQ_EVERY=${IRQ_EVERY:-5}        # 5샘플(100ms)마다 IRQ
EXPCTL=${EXPCTL:-2000}
BPSCTL=${BPSCTL:-4096}
NPROC=$(nproc)

BUS=1; DEV=1-0048; A_ADDR=0x11; B_ADDR=0x12; CSI_IRQ=32e40000.csi

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/fast_$STAMP.log"
DUMP="$OUT/fast_${STAMP}_dumps.txt"
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

kill_cap() {
	pkill -x capapp 2>/dev/null
	for _ in $(seq 1 15); do pgrep -x capapp >/dev/null || return 0; sleep 1; done
	pkill -9 -x capapp 2>/dev/null
	sleep 2
}

restore() {
	rc=$?
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
	log "### 로그: $LOG / 덤프: $DUMP"
	exit $rc
}
trap restore EXIT INT TERM

ap_rd() { i2ctransfer -f -y -a "$1" "w2@$2" "$3" "$4" "r$5" 2>/dev/null | sed 's/0x//g' | tr -d ' '; }
now_us() { local t=${EPOCHREALTIME}; echo $(( ${t%.*} * 1000000 + 10#${t#*.} )); }
irq_of() { awk -v d="$1" -v n="$NPROC" '$NF==d{s=0;for(i=2;i<=n+1;i++)s+=$i;print s+0;exit}' /proc/interrupts; }

# 양 채널 TOTAL_FRAME_TIME + FRAME_CNT 를 i2ctransfer **한 번**으로 (실측 3ms)
read_pair() { # 결과: 전역 RA_T RA_C RB_T RB_C (10진), 실패 시 빈 값
	local o
	o=$(i2ctransfer -f -y -a "$BUS" \
		"w2@$AA" 0x00 0xfc r4 "w2@$AA" 0x00 0x02 r2 \
		"w2@$AB" 0x00 0xfc r4 "w2@$AB" 0x00 0x02 r2 2>/dev/null \
		| sed 's/0x//g; s/ //g' | tr '\n' ' ')
	local -a f
	read -ra f <<<"$o"
	if [ ${#f[@]} -ne 4 ]; then RA_T=""; RA_C=""; RB_T=""; RB_C=""; return 1; fi
	RA_T=$((16#${f[0]})); RA_C=$((16#${f[1]:0:2}))
	RB_T=$((16#${f[2]})); RB_C=$((16#${f[3]:0:2}))
	return 0
}

ap_dump() { # $1=bus $2=addr
	printf '    FRAME_CNT=%s ERROR=%s LINE_TIME=%s FRAME_TIME=%s TOTAL=%s CTRL=%s ATOMIC=%s TRIG=%s MAXFPS=%s MISMATCH=%s\n' \
		"$(ap_rd "$1" "$2" 0x00 0x02 2)" "$(ap_rd "$1" "$2" 0x00 0x06 2)" \
		"$(ap_rd "$1" "$2" 0x00 0xd8 4)" "$(ap_rd "$1" "$2" 0x00 0xe0 4)" \
		"$(ap_rd "$1" "$2" 0x00 0xfc 4)" "$(ap_rd "$1" "$2" 0x10 0x00 2)" \
		"$(ap_rd "$1" "$2" 0x11 0x84 2)" "$(ap_rd "$1" "$2" 0x11 0x86 2)" \
		"$(ap_rd "$1" "$2" 0x20 0x20 2)" "$(ap_rd "$1" "$2" 0x61 0x12 2)"
}

dump_all() { # $1=라벨 $2=trial $3=시각us
	dmp ""
	dmp "======== $1  trial=$2  t=$(( $3 / 1000 ))ms  $(date -Is) ========"
	dmp "  [AP1302 A $AA]"; ap_dump "$BUS" "$AA" >>"$DUMP"
	dmp "  [AP1302 B $AB]"; ap_dump "$BUS" "$AB" >>"$DUMP"
	dmp "  [MAX9296] CTRL3=$(ap_rd "$BUS" 0x48 0x00 0x13 1) CSI_EN=$(ap_rd "$BUS" 0x48 0x03 0x13 1) BACKTOP25=$(ap_rd "$BUS" 0x48 0x03 0x20 1)"
	dmp "  prepare: $(cat "/sys/bus/i2c/devices/$DEV/prepare" 2>/dev/null)"
}

# --- 준비 -------------------------------------------------------------------
if systemctl is-active --quiet cam-operate.service; then
	systemctl stop cam-operate.service >>"$LOG" 2>&1; sleep 5
fi
pkill -x killcam 2>/dev/null
sleep 2
# 스테이징 실패를 조용히 넘기면 이전 회차의 낡은 바이너리가 측정된다.
cp /usr/local/bin/gstApp "$BIN" || { echo "중단: 앱 스테이징 복사 실패"; exit 2; }
chmod +x "$BIN" || { echo "중단: 앱 스테이징 chmod 실패"; exit 2; }

log "=== 동결 전환 고속 샘플링 $(date -Is) ==="
log "간격 $((IVL_US/1000))ms(50Hz), 창 ${SAMPLES_SEC}s x ${TRIALS}회. exp=$EXPCTL, ch2+ch3 듀얼와이드 120fps."
log "IRQ 는 ${IRQ_EVERY}샘플마다. i2c 는 4쌍 결합 1회(실측 3ms)."
log ""
: >"$DUMP"

TOTAL_SAMPLES=$(( SAMPLES_SEC * 1000000 / IVL_US ))

for TR in $(seq 1 "$TRIALS"); do
	log "=============== 시도 $TR / $TRIALS ==============="
	kill_cap
	systemctl is-active --quiet cam-operate.service && { systemctl stop cam-operate.service >>"$LOG" 2>&1; sleep 5; }

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
	    ' "$BACKUP" >"$OUT/.fs.json" || exit 1
	cp "$OUT/.fs.json" "$CONF"
	CONF_DIRTY=1
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	[ "$UNIQ" = "1" ] || log "  !!! 통제 검증 실패 unique=$UNIQ — 무효 !!!"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3
	dmesg -C 2>/dev/null

	CSVT="$OUT/fast_${STAMP}_t${TR}.csv"
	echo "t_us,a_total,a_cnt,b_total,b_cnt,csi_d" >"$CSVT"
	AL="$OUT/fast_${STAMP}_app_t${TR}.log"
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_us)

	AA="$A_ADDR"; AB="$B_ADDR"
	RA_T=""; RA_C=""; RB_T=""; RB_C=""
	PC=$(irq_of "$CSI_IRQ"); ZERO=0; FROZE=0; STARTED=0; FRZ_US=""
	ADDR_OK=0

	for (( i=1; i<=TOTAL_SAMPLES; i++ )); do
		# 서브셸 없이 시각 계산 — 20ms 예산에서 프로세스 spawn 을 없앤다
		_t=${EPOCHREALTIME}; TS=$(( ${_t%.*} * 1000000 + 10#${_t#*.} - T0 ))

		# 펌웨어 로드 전에는 듀얼 주소가 응답하지 않는다. 8초 뒤 한 번 확인.
		if [ "$ADDR_OK" -eq 0 ] && [ "$TS" -ge 8000000 ]; then
			read_pair && ADDR_OK=1
		elif [ "$ADDR_OK" -eq 1 ]; then
			read_pair || true
		fi

		DC=""
		if (( i % IRQ_EVERY == 0 )); then
			CC=$(irq_of "$CSI_IRQ"); DC=$(( CC - PC )); PC=$CC
			[ "$DC" -gt 0 ] && STARTED=1
			if [ "$STARTED" -eq 1 ] && [ "$FROZE" -eq 0 ]; then
				if [ "$DC" -eq 0 ]; then ZERO=$(( ZERO + 1 )); else ZERO=0; fi
				if [ "$ZERO" -ge 3 ]; then
					FROZE=1; FRZ_US=$TS
					log "  -> 동결 t=$(( TS / 1000 ))ms (A=${RA_T:-?} B=${RB_T:-?} us)"
					dump_all "FREEZE" "$TR" "$TS"
				fi
			fi
		fi

		echo "$TS,${RA_T},${RA_C},${RB_T},${RB_C},${DC}" >>"$CSVT"

		_t=${EPOCHREALTIME}
		SLP=$(( T0 + i * IVL_US - ( ${_t%.*} * 1000000 + 10#${_t#*.} ) ))
		if [ "$SLP" -gt 1000 ]; then
			printf -v _s '%d.%06d' $(( SLP / 1000000 )) $(( SLP % 1000000 ))
			sleep "$_s"
		fi
	done

	if [ "$STARTED" -eq 0 ]; then RES="NO_STREAM"
	elif [ "$FROZE" -eq 1 ]; then RES="동결(t=$(( FRZ_US / 1000 ))ms)"
	else RES="완주"; fi
	NROW=$(( $(wc -l <"$CSVT") - 1 ))
	log "  결과: $RES / 샘플 $NROW 행 / 실효간격 $(awk -v n="$NROW" -v s="$SAMPLES_SEC" 'BEGIN{printf "%.1f", s*1000.0/n}')ms"
	log "  드라이버 경고: $(dmesg | grep -icE "max9296.*(warn|err|fail|stale|timeout)" || echo 0)건"
	log ""

	kill_cap
	find /dev/shm -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
	sleep 2
done

log "=== 종료 $(date -Is) ==="
