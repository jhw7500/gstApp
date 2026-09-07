#!/usr/bin/env bash
#
# probe-dual-topology.sh — 듀얼와이드 360p120 실패의 두 가지 판별 실험.
#
# 선행 확정 사실 (transport_20260907_065027, 2회 재현)
#   ch0+ch1 듀얼와이드 640x360@120 에서 CSI2 IRQ 는 정상의 8.6%(20/s vs 232/s), ISI IRQ 는
#   40초 동안 **0회**, 앱 수신 0. 대조 ch0+ch2 는 CSI2 232/s = ISI 117/s x2 로 정상.
#   => 파손은 ISI 가 아니라 그 상류(CSI2 수신기/MIPI/MAX9296 출력)다.
#   남은 가설: 듀얼 모드에서 두 AP1302 의 프레임 동기가 120fps 창(8.33ms)을 못 맞춘다.
#
# 실험 A — ch2+ch3 듀얼와이드 (다른 디시리얼라이저 인스턴스 + 다른 ISI)
#   ch0/ch1 -> 2-0048, csi 32e50000, isi 32e02000 (= DTS isi_1, isi_chain 없음)
#   ch2/ch3 -> 1-0048, csi 32e40000, isi 32e00000 (= DTS isi_0, isi_chain 보유)
#   판별: ch2+ch3@120 도 실패하면 **듀얼 모드 공통 문제**(인스턴스/ISI 무관).
#         정상이면 ch0/ch1 인스턴스 또는 isi_1 경로 고유 문제.
#   대조로 ch2+ch3@30 을 함께 돌려 "이 경로의 듀얼 자체는 된다"를 먼저 세운다.
#
# 실험 B — 앱 없는 독립 파이프라인 (ch0+ch1 듀얼와이드)
#   판별: 독립 경로에서도 실패하면 gstApp/GStreamer 무관한 **드라이버·하드웨어** 문제.
#         정상이면 앱 파이프라인 쪽 문제.
#   주의: 폐기된 raw v4l2 방식(`enable`+`media-ctl` 만)은 `prepare` 를 안 거쳐 링크가 완전
#         구성되지 않아 20~25% 낮게 나온다(memory: 2ch-fps-measurement-2026-09). 그래서 여기서는
#         **`prepare` 를 직접 쓰고 state=CONSUMED 를 확인한 뒤** 캡처한다.
#         prepare 형식: "1 <generation> <width> <height> <fps> <enable>"
#           width 는 듀얼이면 2배(max9296Prepare.cpp:446), enable 은 bit0|bit1<<1, 3=dual-wide.
#   그리고 같은 독립 경로로 @30 대조를 돌려 "독립 경로가 원래 안 되는 것"이 아님을 먼저 세운다.
#
# 공통 계측 (t=0 부터 1초 간격)
#   AP1302 HINF R0x0002[15:8] / 센서주기 R0x00FC  ← 주소는 스트리밍 시작 후 재판별한다
#   CSI2 IRQ (프레임당 2회) / ISI IRQ (프레임당 1회) — 대조군에서 x2 비례 실측 검증됨
#
# 사용법: SAMPLES=40 ./probe-dual-topology.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-40}
IVL_MS=${IVL_MS:-1000}
NPROC=$(nproc)

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/dualtopo_$STAMP.log"
CSV="$OUT/dualtopo_$STAMP.csv"
log() { echo "$*" | tee -a "$LOG"; }

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
# 운영 복원용 prepare/enable 스냅샷
ORIG_EN2=$(cat /sys/bus/i2c/devices/2-0048/enable 2>/dev/null || echo 0)
ORIG_EN1=$(cat /sys/bus/i2c/devices/1-0048/enable 2>/dev/null || echo 0)

kill_cap() {
	pkill -x capapp 2>/dev/null
	pkill -x v4l2-ctl 2>/dev/null
	for _ in $(seq 1 15); do
		pgrep -x capapp >/dev/null || pgrep -x v4l2-ctl >/dev/null || return 0
		sleep 1
	done
	pkill -9 -x capapp 2>/dev/null
	pkill -9 -x v4l2-ctl 2>/dev/null
	sleep 2
}

# 캡처 회차마다 데몬이 꺼져 있음을 **단언**한다. 특히 독립 파이프라인은 cam-operate 가 살아
# 있으면 무효다 — 데몬의 gstApp 이 같은 video 노드와 prepare 지문을 잡아 결과를 오염시키고,
# 드라이버가 한 hw epoch 안의 지문 변경을 -ESTALE 로 거부한다. 가정하지 않고 매번 확인한다.
# (killcam 은 스크립트 시작 시 이미 정지시켰으므로 pkill 명령줄 리터럴은 문제되지 않는다.)
assert_daemon_off() {
	if systemctl is-active --quiet cam-operate.service; then
		log "  !! cam-operate 가 active — 정지 후 진행"
		systemctl stop cam-operate.service >>"$LOG" 2>&1
		sleep 5
	fi
	if pgrep -x gstApp >/dev/null 2>&1; then
		log "  !! gstApp 프로세스 잔존 — 종료"
		pkill -x gstApp 2>/dev/null
		sleep 3
	fi
	log "  데몬 확인: cam-operate=$(systemctl is-active cam-operate.service) gstApp잔존=$(pgrep -cx gstApp 2>/dev/null || echo 0)"
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
	log "  enable 복원: 2-0048=$(cat /sys/bus/i2c/devices/2-0048/enable 2>/dev/null) 1-0048=$(cat /sys/bus/i2c/devices/1-0048/enable 2>/dev/null)"
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
	log "  2-0048 prepare: $(cat /sys/bus/i2c/devices/2-0048/prepare 2>/dev/null)"
	log "### 로그: $LOG"
	log "### CSV: $CSV"
	exit $rc
}
trap restore EXIT INT TERM

# --- 계측 헬퍼 ---------------------------------------------------------------
ap_rd() { i2ctransfer -f -y -a "$1" "w2@$2" "$3" "$4" "r$5" 2>/dev/null | sed 's/0x//g' | tr -d ' '; }
h2d() { [ -n "${1:-}" ] && printf '%d' "$((16#$1))" 2>/dev/null || echo ""; }
now_ms() { date +%s%3N; }

irq_all() {
	awk -v n="$NPROC" '
		$NF=="32e50000.csi" || $NF=="32e40000.csi" ||
		$NF=="32e02000.isi" || $NF=="32e00000.isi" {
			s=0; for (i=2; i<=n+1; i++) s+=$i; printf "%s %d\n", $NF, s
		}' /proc/interrupts
}

pick_addr() { # $1=bus $2=콤마구분 후보
	b=$1
	for a in $(echo "$2" | tr ',' ' '); do
		[ -n "$(ap_rd "$b" "$a" 0x00 0x02 2)" ] && { echo "$a"; return 0; }
	done
	echo ""
}

declare -A IRQP
declare -A HINFP
declare -A ADDR

# 샘플링 루프. 호출 전에 캡처를 띄우고 T0 를 잡아 둔다.
# $1=라벨 $2=trial $3=DEVSPEC("이름=버스:후보주소" 공백구분) $4=T0(ms)
sample_loop() {
	SL_LB=$1; SL_TR=$2; SL_DEVS=$3; SL_T0=$4
	HINFP=()
	ADDR=()
	for spec in $SL_DEVS; do ADDR[${spec%%=*}]=""; done

	{
		printf "  %-7s %8s %8s %8s %8s %8s %8s" \
			"t_ms" "csi0_d" "csi0fps" "isi0_d" "isi0fps" "csi1_d" "isi1_d"
		for spec in $SL_DEVS; do printf " %9s %9s" "${spec%%=*}_dHINF" "${spec%%=*}_sfps"; done
		printf "\n"
		printf "  %s\n" "-------------------------------------------------------------------------------------------------"
	} | tee -a "$LOG"

	for i in $(seq 1 "$SAMPLES"); do
		TS=$(( $(now_ms) - SL_T0 ))

		declare -A IRQC
		while read -r k v; do IRQC[$k]=$v; done < <(irq_all)
		D_CSI0=$(( ${IRQC[32e50000.csi]:-0} - ${IRQP[32e50000.csi]:-0} ))
		D_ISI0=$(( ${IRQC[32e02000.isi]:-0} - ${IRQP[32e02000.isi]:-0} ))
		D_CSI1=$(( ${IRQC[32e40000.csi]:-0} - ${IRQP[32e40000.csi]:-0} ))
		D_ISI1=$(( ${IRQC[32e00000.isi]:-0} - ${IRQP[32e00000.isi]:-0} ))
		for k in "${!IRQC[@]}"; do IRQP[$k]=${IRQC[$k]}; done

		ROW=""; CSVX=""
		for spec in $SL_DEVS; do
			NM=${spec%%=*}; BA=${spec#*=}; BUS=${BA%%:*}; CAND=${BA##*:}
			# 스트리밍이 시작돼야 듀얼 주소(0x11/0x12)가 응답한다. 5초 전에는 판별을 확정하지
			# 않는다 — 펌웨어 로드 전에 잡으면 0x3c 로 잘못 고정된다(선행 실행의 실측 결함).
			if [ -z "${ADDR[$NM]}" ] && [ "$TS" -ge 8000 ]; then
				ADDR[$NM]=$(pick_addr "$BUS" "$CAND")
			fi
			A=${ADDR[$NM]}
			if [ -z "$A" ]; then
				ROW="$ROW $(printf '%9s %9s' '-' '-')"; CSVX="$CSVX,,"
				continue
			fi
			CUR=$(h2d "$(ap_rd "$BUS" "$A" 0x00 0x02 2 | cut -c1-2)")
			TF=$(h2d "$(ap_rd "$BUS" "$A" 0x00 0xfc 4)")
			SF=$(awk -v v="${TF:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print "-"}')
			P=${HINFP[$NM]:-}
			if [ -n "$P" ] && [ -n "$CUR" ]; then D=$(( (CUR - P + 256) % 256 )); else D="-"; fi
			[ -n "$CUR" ] && HINFP[$NM]=$CUR
			ROW="$ROW $(printf '%9s %9s' "$D" "$SF")"
			CSVX="$CSVX,$D,$SF"
		done

		CF=$(awk -v d="$D_CSI0" -v ms="$IVL_MS" 'BEGIN{printf "%.1f", d/2.0*1000.0/ms}')
		IF=$(awk -v d="$D_ISI0" -v ms="$IVL_MS" 'BEGIN{printf "%.1f", d*1000.0/ms}')

		printf "  %-7s %8s %8s %8s %8s %8s %8s%s\n" \
			"$TS" "$D_CSI0" "$CF" "$D_ISI0" "$IF" "$D_CSI1" "$D_ISI1" "$ROW" | tee -a "$LOG"
		echo "$SL_LB,$SL_TR,$TS,$D_CSI0,$CF,$D_ISI0,$IF,$D_CSI1,$D_ISI1${CSVX}" >>"$CSV"

		NEXT=$(( SL_T0 + i * IVL_MS ))
		SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done

	log "  판별된 AP1302 주소: $(for spec in $SL_DEVS; do printf '%s=%s ' "${spec%%=*}" "${ADDR[${spec%%=*}]:--}"; done)"
}

banner() {
	log "==============================================================="
	log "### $1"
	log "==============================================================="
}

# --- 실험 A: 앱 기반 -------------------------------------------------------
probe_app() { # $1=라벨 $2=trial $3=fps $4..$7=ch0..ch3 $8=DEVSPEC
	LB=$1; TR=$2; FPS=$3; C0=$4; C1=$5; C2=$6; C3=$7; DEVS=$8
	banner "$LB  시도 $TR  (앱 경유, ${FPS}fps)"
	kill_cap
	assert_daemon_off

	jq --argjson f "$FPS" --argjson c0 "$C0" --argjson c1 "$C1" \
	   --argjson c2 "$C2" --argjson c3 "$C3" '
	      .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=$f
	    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
	    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
	    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
	    | .VHL_CAM.i2c2.ch0.led_flash.enable=false | .VHL_CAM.i2c2.ch1.led_flash.enable=false
	    | .VHL_CAM.i2c1.ch2.led_flash.enable=false | .VHL_CAM.i2c1.ch3.led_flash.enable=false
	    ' "$BACKUP" >"$OUT/.dt.json" || return 1
	cp "$OUT/.dt.json" "$CONF"
	CONF_DIRTY=1

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/dualtopo_app_${LB}_${TR}.log"
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  prepare 2-0048: $(cat /sys/bus/i2c/devices/2-0048/prepare 2>/dev/null)"
	log "  prepare 1-0048: $(cat /sys/bus/i2c/devices/1-0048/prepare 2>/dev/null)"
	log "  앱 preroll/NO DATA: $(grep -cE "NOT prerolled|NO DATA for" "$AL" 2>/dev/null || echo 0)건"
	log "  앱 enc-stat in>0 줄 수: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0)"
	log "  앱 enc-stat 마지막: $(grep -oE 'ch[0-9] enc-stat\[[0-9]+s\] in=[0-9]+\([0-9.]+fps\)' "$AL" 2>/dev/null | tail -2 | tr '\n' ' ')"
	log ""
	kill_cap
	find /dev/shm -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
	sleep 2
}

# --- 실험 B: 독립 파이프라인 (앱 없음) --------------------------------------
probe_raw() { # $1=라벨 $2=trial $3=fps $4=csi(0|1) $5=enable비트 $6=DEVSPEC
	LB=$1; TR=$2; FPS=$3; CSI=$4; EN=$5; DEVS=$6
	banner "$LB  시도 $TR  (독립 파이프라인, ${FPS}fps)"
	kill_cap
	assert_daemon_off

	case "$CSI" in
	0) DEV=2-0048; VID=/dev/video4; SD=2 ;;
	1) DEV=1-0048; VID=/dev/video3; SD=3 ;;
	*) log "  잘못된 csi=$CSI"; return 1 ;;
	esac
	# 듀얼(enable=3)은 폭 2배 (max9296Prepare.cpp:446)
	if [ "$EN" -eq 3 ]; then W=1280; else W=640; fi
	H=360

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	echo "$EN" >"/sys/bus/i2c/devices/$DEV/enable" 2>>"$LOG"
	GEN=$(date +%s%N)
	log "  prepare 쓰기: \"1 $GEN $W $H $FPS $EN\" -> $DEV"
	if ! printf '1 %s %s %s %s %s\n' "$GEN" "$W" "$H" "$FPS" "$EN" \
		>"/sys/bus/i2c/devices/$DEV/prepare" 2>>"$LOG"; then
		log "  !! prepare 쓰기 실패 — 이 회차는 무효"
		return 1
	fi
	for _ in $(seq 1 20); do
		PS=$(cat "/sys/bus/i2c/devices/$DEV/prepare" 2>/dev/null)
		case "$PS" in *state=CONSUMED*|*state=READY*) break ;; esac
		sleep 1
	done
	log "  prepare 상태: $PS"
	case "$PS" in
	*state=CONSUMED*|*state=READY*) ;;
	*) log "  !! prepare 가 CONSUMED/READY 에 못 감 — 폐기된 raw 방식과 같아지므로 이 회차는 무효"
	   return 1 ;;
	esac

	MC=$(media-ctl -V "\"max9296 $SD\":0 [fmt:UYVY8_2X8/${W}x${H}@1/${FPS}]" 2>&1)
	[ -n "$MC" ] && log "  media-ctl: $MC"

	RL="$OUT/dualtopo_raw_${LB}_${TR}.log"
	CNT=$(( (SAMPLES + 25) * FPS ))
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid timeout $((SAMPLES + 30)) v4l2-ctl -d "$VID" \
		--set-fmt-video=width=${W},height=${H},pixelformat=RGBP \
		--stream-mmap --stream-count="$CNT" </dev/null >"$RL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  prepare 후상태: $(cat "/sys/bus/i2c/devices/$DEV/prepare" 2>/dev/null)"
	log "  v4l2-ctl fps 보고: $(grep -oE '[0-9]+\.[0-9]+ fps' "$RL" 2>/dev/null | tail -3 | tr '\n' ' ')"
	log "  v4l2-ctl 오류: $(grep -iE "error|fail|VIDIOC" "$RL" 2>/dev/null | head -3 | tr '\n' ' ')"
	log ""
	kill_cap
	sleep 2
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

log "=== 듀얼와이드 토폴로지·독립경로 판별 $(date -Is) ==="
log "샘플 ${SAMPLES}회 x ${IVL_MS}ms, 캡처 기동 직후부터. AP1302 주소는 t>=8s 에 판별."
log "설정 백업 md5=$ORIG_MD5 / 시험 전 live md5=$LIVE_MD5"
log "시험 전 enable: 2-0048=$ORIG_EN2 1-0048=$ORIG_EN1"
log ""

echo "case,trial,t_ms,csi0_d,csi0_fps,isi0_d,isi0_fps,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"

# ── 실험 A: ch2+ch3 듀얼와이드 ───────────────────────────────────────────────
# 먼저 @30 으로 "이 경로의 듀얼 자체는 된다"를 세운 뒤 @120 을 본다.
probe_app "A0_ch2ch3_dual_30"  1 30  false false true true "ch2=1:0x11,0x3c ch3=1:0x12,0x3c"
probe_app "A1_ch2ch3_dual_120" 1 120 false false true true "ch2=1:0x11,0x3c ch3=1:0x12,0x3c"
probe_app "A1_ch2ch3_dual_120" 2 120 false false true true "ch2=1:0x11,0x3c ch3=1:0x12,0x3c"

# ── 실험 B: 앱 없는 독립 파이프라인 (ch0+ch1 듀얼와이드) ──────────────────────
# @30 대조를 먼저 세워 독립 경로 자체가 동작함을 증명한 뒤 @120 을 본다.
probe_raw "B0_raw_ch0ch1_dual_30"  1 30  0 3 "ch0=2:0x11,0x3c ch1=2:0x12,0x3c"
probe_raw "B1_raw_ch0ch1_dual_120" 1 120 0 3 "ch0=2:0x11,0x3c ch1=2:0x12,0x3c"

log "=== 종료 $(date -Is) ==="
