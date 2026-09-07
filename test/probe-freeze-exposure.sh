#!/usr/bin/env bash
#
# probe-freeze-exposure.sh — 동결의 **노출 의존성**을 검증한다.
#
# 확정된 것 (frz_20260907_103402, 6회 + 선행 12회)
#   동결 = 듀얼 쌍의 **한 채널만 수직 블랭킹이 약 2.1배로 늘어나** 두 채널 주기가 어긋나고
#   합성 와이드 프레임이 성립하지 않는 것. 착지값이 재현적이다(9,560 / 9,581us = 104.6/104.4fps,
#   21us 차이). 유효 프레임시간(R0x00E0=7,344us)·라인시간·트리거 설정·링크는 전부 불변이고
#   AP1302 ERROR 는 0x0000, dmesg 도 조용하다.
#   착지값은 104.x(우세) 또는 60~61(정확히 절반) 두 군으로 뭉친다.
#
# 왜 노출인가
#   exp=2000 조건에서는 동결이 나고(이번 2/6, 선행 3/4), 정본 조건 exp=10000 에서는 7회 0건이었다.
#   그러나 정본 회차는 채널·bps 등 다른 것도 함께 달랐고 표본도 작아 **상관은 추정**이다.
#   여기서는 **노출만** 바꿔 동결률과 착지값을 비교한다.
#
# 설계 (3 갈래 x ROUNDS 회, 교차 배치. 항상 ch2+ch3 듀얼와이드 640x360@120, 창 90s)
#   exp=2000   현재까지 동결이 관측된 값
#   exp=5000   120fps 주기(8,333us)의 0.6배 — 드라이버가 fps 에서 유도하는 기본값
#   exp=10000  정본값. 프레임주기를 넘는다
#   노출 외 모든 항목은 4채널 동일값으로 누르고 회차마다 unique=1 로 검증한다.
#
# 동결 없이 완주한 회차도 버리지 않는다 — BEFORE(t=25s)/END(창 종료) 두 시점의
#   TOTAL_FRAME_TIME 을 양 채널 모두 기록해 **블랭킹 표류**를 본다. 동결은 그 표류의 끝일 수 있다.
#
# 사용법: SAMPLES=90 ROUNDS=5 ./probe-freeze-exposure.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-90}
ROUNDS=${ROUNDS:-5}
EXPS=${EXPS:-"2000 5000 10000"}
IVL_MS=${IVL_MS:-1000}
BPSCTL=${BPSCTL:-4096}
BEFORE_AT=${BEFORE_AT:-25000}   # 안정 구간 기준 덤프 시각(ms)
NPROC=$(nproc)

# ch2+ch3 듀얼와이드 (동결 관측률이 가장 높았던 조합)
BUS=1; DEV=1-0048; A_ADDR=0x11; B_ADDR=0x12; CSI_IRQ=32e40000.csi; ISI_IRQ=32e00000.isi

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/frzexp_$STAMP.log"
CSV="$OUT/frzexp_$STAMP.csv"
SUM="$OUT/frzexp_${STAMP}_summary.csv"
DUMP="$OUT/frzexp_${STAMP}_dumps.txt"
log() { echo "$*" | tee -a "$LOG"; }
dmp() { echo "$*" >>"$DUMP"; }

[ -e "$BACKUP" ] || { echo "백업 없음: $BACKUP"; exit 2; }
[ -x "$RESET" ] || { echo "리셋 스크립트 없음: $RESET"; exit 2; }
ORIG_MD5=$(md5sum "$BACKUP" | awk '{print $1}')
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
	cp "$BACKUP" "$CONF"
	NOW=$(md5sum "$CONF" | awk '{print $1}')
	if [ "$NOW" = "$ORIG_MD5" ]; then
		log "  설정 복원 md5 일치 ($NOW)"
	else
		log "  !!! 설정 복원 md5 불일치: $NOW != $ORIG_MD5 — 수동 확인 필요 !!!"
	fi
	"$RESET" -q >>"$LOG" 2>&1
	sleep 2
	echo "$ORIG_EN2" >/sys/bus/i2c/devices/2-0048/enable 2>/dev/null
	echo "$ORIG_EN1" >/sys/bus/i2c/devices/1-0048/enable 2>/dev/null
	[ "$WAS" -eq 1 ] && { systemctl start cam-operate.service >>"$LOG" 2>&1; sleep 12; }
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
cp /usr/local/bin/gstApp "$BIN" && chmod +x "$BIN"

log "=== 동결 포렌식 $(date -Is) ==="
log "조건: 통제 config(4채널 동일), bps=$BPSCTL, ch2+ch3 듀얼와이드 640x360@120. 노출 갈래: $EXPS"
log "창 ${SAMPLES}s, ${ROUNDS}라운드 x 갈래 교차. CSI2 증분 2회 연속 0 이면 즉시 덤프."
log ""
echo "arm,trial,t_ms,csi_d,isi_d,a_dhinf,a_sfps,b_dhinf,b_sfps,state" >"$CSV"
: >"$DUMP"

run_one() { # $1=exp $2=trial
	EXP=$1; TR=$2; ARM="exp$EXP"
	log "==============================================================="
	log "### $ARM  시도 $TR  (통제 config, ch2+ch3 듀얼와이드 120fps, 창 ${SAMPLES}s)"
	log "==============================================================="
	kill_cap
	if systemctl is-active --quiet cam-operate.service; then
		systemctl stop cam-operate.service >>"$LOG" 2>&1; sleep 5
	fi

	jq --argjson e "$EXP" --argjson b "$BPSCTL" '
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
	    ' "$BACKUP" >"$OUT/.fz3.json" || return 1
	cp "$OUT/.fz3.json" "$CONF"
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	VALID=1
	[ "$UNIQ" = "1" ] || { log "  !!! 통제 검증 실패 (unique=$UNIQ) — 무효 !!!"; VALID=0; }
	log "  exp 확인: $(jq -r '[.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]|@csv' "$CONF")"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	dmesg -C 2>/dev/null
	DK="$OUT/frzexp_${STAMP}_dmesg_${ARM}_t${TR}.txt"
	dmesg -w >"$DK" 2>/dev/null &
	DMESG_PID=$!

	AL="$OUT/frzexp_${STAMP}_app_${ARM}_t${TR}.log"
	PC=$(irq_of "$CSI_IRQ"); PI=$(irq_of "$ISI_IRQ")
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	AA=""; AB=""; PHA=""; PHB=""
	ZERO=0; FROZE=0; FRZ_MS=""; DIDBEFORE=0; STARTED=0
	TAB=""; TBB=""; TAE=""; TBE=""      # TOTAL_FRAME_TIME: A/B 의 BEFORE 와 END
	log "  t_ms     csi_d  isi_d   A_dHINF  A_sfps    B_dHINF  B_sfps   state"

	for i in $(seq 1 "$SAMPLES"); do
		TS=$(( $(now_ms) - T0 ))
		CC=$(irq_of "$CSI_IRQ"); CI=$(irq_of "$ISI_IRQ")
		DC=$(( CC - PC )); DI=$(( CI - PI )); PC=$CC; PI=$CI

		if [ -z "$AA" ] && [ "$TS" -ge 8000 ]; then
			AA=$(pick_addr "$BUS" "$A_ADDR"); AB=$(pick_addr "$BUS" "$B_ADDR")
		fi
		DA="-"; SA="-"; DB="-"; SB="-"
		if [ -n "$AA" ]; then
			CA=$(h2d "$(ap_rd "$BUS" "$AA" 0x00 0x02 2 | cut -c1-2)")
			TA=$(h2d "$(ap_rd "$BUS" "$AA" 0x00 0xfc 4)")
			[ -n "$TA" ] && [ "$TA" -gt 0 ] && TAE=$TA
			SA=$(awk -v v="${TA:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print "-"}')
			[ -n "$PHA" ] && [ -n "$CA" ] && DA=$(( (CA - PHA + 256) % 256 ))
			[ -n "$CA" ] && PHA=$CA
		fi
		if [ -n "$AB" ]; then
			CB=$(h2d "$(ap_rd "$BUS" "$AB" 0x00 0x02 2 | cut -c1-2)")
			TB=$(h2d "$(ap_rd "$BUS" "$AB" 0x00 0xfc 4)")
			[ -n "$TB" ] && [ "$TB" -gt 0 ] && TBE=$TB
			SB=$(awk -v v="${TB:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print "-"}')
			[ -n "$PHB" ] && [ -n "$CB" ] && DB=$(( (CB - PHB + 256) % 256 ))
			[ -n "$CB" ] && PHB=$CB
		fi

		STATE="-"
		[ "$DC" -gt 0 ] && STARTED=1

		if [ "$STARTED" -eq 1 ] && [ "$DIDBEFORE" -eq 0 ] && [ "$TS" -ge "$BEFORE_AT" ]; then
			dump_all "BEFORE(정상) $ARM" "$TR" "$TS"; DIDBEFORE=1; STATE="BEFORE덤프"
			TAB=$TAE; TBB=$TBE
		fi

		if [ "$STARTED" -eq 1 ] && [ "$FROZE" -eq 0 ]; then
			if [ "$DC" -eq 0 ]; then ZERO=$(( ZERO + 1 )); else ZERO=0; fi
			if [ "$ZERO" -ge 2 ]; then
				FROZE=1; FRZ_MS=$TS; STATE="**동결**"
				log "    -> 동결 감지 t=${TS}ms"
				dump_all "FREEZE $ARM" "$TR" "$TS"
				dmp "  [dmesg 마지막 15줄]"
				tail -15 "$DK" 2>/dev/null | sed 's/^/    /' >>"$DUMP"
			fi
		fi

		printf "  %-8s %6s %6s   %7s %7s    %7s %7s   %s\n" \
			"$TS" "$DC" "$DI" "$DA" "$SA" "$DB" "$SB" "$STATE" | tee -a "$LOG"
		echo "$ARM,$TR,$TS,$DC,$DI,$DA,$SA,$DB,$SB,$STATE" >>"$CSV"

		NEXT=$(( T0 + i * IVL_MS )); SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done

	# 동결 여부와 무관하게 창 종료 시점을 남긴다 (완주 회차의 블랭킹 표류도 본다)
	dump_all "END $ARM" "$TR" "$(( $(now_ms) - T0 ))"
	if [ "$STARTED" -eq 0 ]; then RES="NO_STREAM"
	elif [ "$FROZE" -eq 1 ]; then RES="동결"
	else RES="완주"; fi
	log "  결과: $RES ${FRZ_MS:+(t=${FRZ_MS}ms)} / A total ${TAB:-?} -> ${TAE:-?} us / B total ${TBB:-?} -> ${TBE:-?} us"
	log "  앱 enc-stat in>0: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0)줄"
	log "  드라이버 경고/에러: $(grep -icE "max9296.*(warn|err|fail|stale|timeout)" "$DK" 2>/dev/null || echo 0)건"
	echo "$ARM,$TR,$RES,${FRZ_MS},${TAB},${TAE},${TBB},${TBE},$VALID" >>"$SUM"
	log ""

	kill "$DMESG_PID" 2>/dev/null; DMESG_PID=""
	kill_cap
	find /dev/shm -name '*.mp4*' -mmin -10 -delete 2>/dev/null
	sleep 2
}

echo "arm,trial,result,freeze_ms,A_total_before,A_total_end,B_total_before,B_total_end,valid" >"$SUM"
for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	for e in $EXPS; do run_one "$e" "$r"; done
done

log "=== 종료 $(date -Is) ==="
log "### 요약"
column -s, -t "$SUM" 2>/dev/null | sed 's/^/  /' | tee -a "$LOG" || cat "$SUM"
