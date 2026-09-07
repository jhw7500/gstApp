#!/usr/bin/env bash
#
# probe-freeze-pollrate.sh — **i2c 폴링 빈도가 동결을 억제하는가**를 가른다.
#
# 왜 이 시험이 필요한가
#   같은 조건(통제 config, exp=2000, ch2+ch3 듀얼와이드 640x360@120, 창 90s)에서
#     1초 폴링:  forensics 2/6 + exposure 2000갈래 1/5 = **3/11 동결**
#     20ms 폴링: fastsample **0/8 동결**
#   8회 연속 무동결은 27% 가정 하에서 확률 약 9% — 아주 이상하진 않지만,
#   **50Hz 폴링이 i2c 트래픽을 초당 1회에서 50회로 늘린 새 변수**라는 점을 배제할 수 없다.
#   억제가 사실이라면 그 자체가 "i2c 접근이 AP1302 타이밍에 관여한다"는 강한 단서이고,
#   억제가 아니라면 fastsample 의 0/8 은 우연이므로 고속 관측을 계속 밀어붙일 수 있다.
#
# 설계 (2 갈래 x ROUNDS 회, 교차 배치)
#   slow : i2c 를 50샘플(1초)마다  <- 동결이 관측됐던 조건
#   fast : i2c 를 매 샘플(20ms)마다 <- 동결이 안 나온 조건
#   **루프는 두 갈래 모두 20ms 로 동일하게 돈다.** IRQ 도 두 갈래 모두 5샘플(100ms)마다 읽는다.
#   따라서 bash 루프 부하·IRQ 접근·판정 로직이 전부 같고 **i2c 트랜잭션 수만 50배 차이**난다.
#   동결 판정도 두 갈래 모두 IRQ 기준(3회 연속 증분 0 = 300ms)으로 동일하다.
#
# 확정된 배경 (fast_20260907_113226, 8/8 재현)
#   정상 동작 중에도 프레임시간 이탈이 분당 30~50회 일어나고 33/35 가 20ms 안에 복귀한다
#   (최장 60ms). 이탈 정점은 15,400~18,000us(56~65fps)로 동결 착지값과 같은 영역이다.
#   평균은 8,388us(σ383)라 1초 간격으로는 이 이탈이 보이지 않는다.
#
# 사용법: SAMPLES_SEC=90 ROUNDS=10 ./probe-freeze-pollrate.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES_SEC=${SAMPLES_SEC:-90}
ROUNDS=${ROUNDS:-10}
I2C_EVERY_SLOW=${I2C_EVERY_SLOW:-50}   # 50샘플 = 1초
IVL_US=${IVL_US:-20000}          # 20ms = 50Hz
IRQ_EVERY=${IRQ_EVERY:-5}        # 5샘플(100ms)마다 IRQ
EXPCTL=${EXPCTL:-2000}
BPSCTL=${BPSCTL:-4096}
NPROC=$(nproc)

BUS=1; DEV=1-0048; A_ADDR=0x11; B_ADDR=0x12; CSI_IRQ=32e40000.csi

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/poll_$STAMP.log"
DUMP="$OUT/poll_${STAMP}_dumps.txt"
SUM="$OUT/poll_${STAMP}_summary.csv"
log() { echo "$*" | tee -a "$LOG"; }
dmp() { echo "$*" >>"$DUMP"; }

[ -e "$BACKUP" ] || { echo "백업 없음: $BACKUP"; exit 2; }
[ -x "$RESET" ] || { echo "리셋 스크립트 없음: $RESET"; exit 2; }
ORIG_MD5=$(md5sum "$BACKUP" | awk '{print $1}')
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
cp /usr/local/bin/gstApp "$BIN" && chmod +x "$BIN"

log "=== i2c 폴링 빈도 A/B $(date -Is) ==="
log "루프 $((IVL_US/1000))ms 고정, 창 ${SAMPLES_SEC}s, ${ROUNDS}라운드 x (slow|fast) 교차. exp=$EXPCTL, ch2+ch3 듀얼와이드 120fps."
log "IRQ 는 두 갈래 모두 ${IRQ_EVERY}샘플(100ms)마다. i2c 만 slow=${I2C_EVERY_SLOW}샘플 / fast=1샘플."
log ""
: >"$DUMP"

TOTAL_SAMPLES=$(( SAMPLES_SEC * 1000000 / IVL_US ))

run_one() { # $1=arm(slow|fast) $2=trial
	ARM=$1; TR=$2
	if [ "$ARM" = "fast" ]; then I2C_EVERY=1; else I2C_EVERY=$I2C_EVERY_SLOW; fi
	log "=============== $ARM  시도 $TR  (i2c 매 ${I2C_EVERY}샘플 = $(( I2C_EVERY * IVL_US / 1000 ))ms) ==============="
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
	    ' "$BACKUP" >"$OUT/.pr.json" || exit 1
	cp "$OUT/.pr.json" "$CONF"
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	[ "$UNIQ" = "1" ] || log "  !!! 통제 검증 실패 unique=$UNIQ — 무효 !!!"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3
	dmesg -C 2>/dev/null

	CSVT="$OUT/poll_${STAMP}_${ARM}_t${TR}.csv"
	echo "t_us,a_total,a_cnt,b_total,b_cnt,csi_d" >"$CSVT"
	AL="$OUT/poll_${STAMP}_app_${ARM}_t${TR}.log"
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_us)

	AA="$A_ADDR"; AB="$B_ADDR"
	RA_T=""; RA_C=""; RB_T=""; RB_C=""
	PC=$(irq_of "$CSI_IRQ"); ZERO=0; FROZE=0; STARTED=0; FRZ_US=""
	ADDR_OK=0

	for (( i=1; i<=TOTAL_SAMPLES; i++ )); do
		# 서브셸 없이 시각 계산 — 20ms 예산에서 프로세스 spawn 을 없앤다
		_t=${EPOCHREALTIME}; TS=$(( ${_t%.*} * 1000000 + 10#${_t#*.} - T0 ))

		# i2c 는 갈래별 주기로만 읽는다 — 루프 부하는 두 갈래가 동일하고 i2c 트랜잭션 수만 다르다.
		if (( i % I2C_EVERY == 0 )); then
			if [ "$ADDR_OK" -eq 0 ] && [ "$TS" -ge 8000000 ]; then
				read_pair && ADDR_OK=1
			elif [ "$ADDR_OK" -eq 1 ]; then
				read_pair || true
			fi
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
	echo "$ARM,$TR,$RES,${FRZ_US:-},${RA_T:-},${RB_T:-}" >>"$SUM"
	log ""

	kill_cap
	find /dev/shm -name '*.mp4*' -mmin -10 -delete 2>/dev/null
	sleep 2
}

echo "arm,trial,result,freeze_us,a_total_last,b_total_last" >"$SUM"
for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	run_one slow "$r"
	run_one fast "$r"
done


log "=== 종료 $(date -Is) ==="
