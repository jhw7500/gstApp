#!/usr/bin/env bash
#
# probe-transport-t0.sh — 듀얼와이드 640x360@120 실패에서 **전송 4계층을 t=0 부터** 센다.
#
# 왜 이 스크립트가 필요한가
# -------------------------
# 선행 측정 `which-ch-died.sh` 는 앱 기동 뒤 `sleep 15` 를 하고서야 샘플링을 시작한다.
# 그 결과 "기동 후 10~12초 뒤 ch0 사망" 이라는 서술이 나왔지만, 실제로는 표의 시각에
# **+15초** 를 더해야 한다(= 약 27초). 그리고 같은 실행의 앱 로그는 enc-stat 5초 윈도우
# **12개 전부 in=0** 이었고 preroll 실패는 PLAYING 요청 15초 뒤에 이미 확정됐다.
# 즉 **앱은 ch0 이 살아 있는 동안에도 한 장도 받지 못했다** — ch0 사망은 원인이 아니다.
#
# 그래서 갈라야 할 질문이 바뀐다:
#   "사망 이전 구간(앱 기동 0~15초)에 프레임이 CSI2/ISI 까지 올라오는가?"
#     - 올라온다  -> 링크는 살아 있고 막힌 곳은 ISI->앱 사이. ch0 사망은 후행 결과.
#     - 안 올라온다 -> ISP 는 만드는데 SerDes/CSI2 가 못 받는다. 전송 자체가 처음부터 실패.
#
# 계층 (상류 -> 하류)
#   1. AP1302 HINF   R0x0002[15:8]  ISP 가 호스트로 내보낸 프레임 (8비트 순환)
#   2. CSI2 IRQ      32e50000.csi   프레임당 2회(FS+FE) -> 증분/2 = 프레임
#   3. ISI  IRQ      32e02000.isi   SoC 캡처
#   4. 앱 enc-stat   enc_stat_sec=1 앱이 실제로 받은 프레임
#
# 측정 설계상의 정정 두 가지
#   - **샘플링 간격 1초.** 120fps 에서 8비트 HINF 는 2.13초면 한 바퀴 돈다. 2초 간격은
#     순환 주기의 94% 라서 `dHINF=0` 이 "0장"인지 "정확히 256장"인지 구분되지 않는다.
#     1초면 최대 증분이 약 120 이라 순환이 원천 배제된다 (docs/health-raw-v1.md:111 과 동일 결론).
#   - **enc_stat_sec=1.** 5초 윈도우로는 앱 수신이 언제 끊겼는지 못 가린다.
#
# 주의
#   - AP1302 i2c 주소는 모드마다 다르다 — 듀얼 0x11/0x12, 단일 0x3c. 응답으로 자동 판별한다.
#     (주소를 잘못 잡으면 전 채널 무응답이 나와 "죽었다"로 오독한다.)
#   - 360p@120 에서는 스트리밍 중 v4l2 노출 변경이 파이프라인을 죽인다. 노출은 건드리지 않는다.
#   - config 는 md5 백업 + trap 으로 어떤 종료 경로에서도 복원한다.
#
# 사용법:  SAMPLES=40 IVL=1 ./probe-transport-t0.sh
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
LOG="$OUT/transport_$STAMP.log"
CSV="$OUT/transport_$STAMP.csv"
log() { echo "$*" | tee -a "$LOG"; }

[ -e "$BACKUP" ] || { echo "백업 없음: $BACKUP"; exit 2; }
[ -x "$RESET" ] || { echo "리셋 스크립트 없음: $RESET"; exit 2; }
ORIG_MD5=$(md5sum "$BACKUP" | awk '{print $1}')
LIVE_MD5=$(md5sum "$CONF" | awk '{print $1}')
WAS=0
systemctl is-active --quiet cam-operate.service && WAS=1

kill_app() {
	pkill -x capapp 2>/dev/null
	for _ in $(seq 1 15); do pgrep -x capapp >/dev/null || return 0; sleep 1; done
	pkill -9 -x capapp 2>/dev/null
	sleep 2
}

restore() {
	rc=$?
	log ""
	log "### 복구"
	kill_app
	cp "$BACKUP" "$CONF"
	NOW=$(md5sum "$CONF" | awk '{print $1}')
	if [ "$NOW" = "$ORIG_MD5" ]; then
		log "  설정 복원 md5 일치 ($NOW)"
	else
		log "  !!! 설정 복원 md5 불일치: $NOW != $ORIG_MD5 — 수동 확인 필요 !!!"
	fi
	"$RESET" -q >>"$LOG" 2>&1
	sleep 2
	[ "$WAS" -eq 1 ] && { systemctl start cam-operate.service >>"$LOG" 2>&1; sleep 12; }
	log "  cam-operate: $(systemctl is-active cam-operate.service)"
	log "### 로그: $LOG"
	log "### CSV: $CSV"
	exit $rc
}
trap restore EXIT INT TERM

# --- 계측 헬퍼 ---------------------------------------------------------------
ap_rd() { # $1=bus $2=addr $3,$4=reg hi,lo $5=len
	i2ctransfer -f -y -a "$1" "w2@$2" "$3" "$4" "r$5" 2>/dev/null | sed 's/0x//g' | tr -d ' '
}
h2d() { [ -n "${1:-}" ] && printf '%d' "$((16#$1))" 2>/dev/null || echo ""; }
now_ms() { date +%s%3N; }

# /proc/interrupts 를 한 번만 읽어 관심 IRQ 4개를 동시에 뽑는다 (샘플당 awk 1회).
irq_all() {
	awk -v n="$NPROC" '
		$NF=="32e50000.csi" || $NF=="32e40000.csi" ||
		$NF=="32e02000.isi" || $NF=="32e00000.isi" {
			s=0; for (i=2; i<=n+1; i++) s+=$i; printf "%s %d\n", $NF, s
		}' /proc/interrupts
}

# 응답하는 첫 주소를 고른다. 없으면 빈 문자열.
pick_addr() { # $1=bus $2=콤마구분 후보
	b=$1
	for a in $(echo "$2" | tr ',' ' '); do
		[ -n "$(ap_rd "$b" "$a" 0x00 0x02 2)" ] && { echo "$a"; return 0; }
	done
	echo ""
}

# --- 준비 -------------------------------------------------------------------
if systemctl is-active --quiet cam-operate.service; then
	systemctl stop cam-operate.service >>"$LOG" 2>&1
	sleep 5
fi
pkill -x killcam 2>/dev/null
sleep 2
cp /usr/local/bin/gstApp "$BIN" && chmod +x "$BIN"

log "=== 전송 4계층 t=0 프로브 $(date -Is) ==="
log "샘플 ${SAMPLES}회 x ${IVL_MS}ms, 앱 기동 직후부터. HINF 순환(120fps=2.13s) 배제 위해 1초 간격."
log "설정 백업 md5=$ORIG_MD5 / 시험 전 live md5=$LIVE_MD5"
[ "$ORIG_MD5" = "$LIVE_MD5" ] || log "  주의: live 가 백업과 다름 — 복원은 백업 기준으로 한다."
log ""

echo "case,trial,t_ms,csi0_d,csi0_fps,isi0_d,isi0_fps,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"

declare -A IRQP
declare -A HINFP

probe() { # $1=라벨 $2=trial $3..$6=ch0..ch3 enable $7=DEVS("이름=버스:후보주소" 공백구분)
	LB=$1; TR=$2; C0=$3; C1=$4; C2=$5; C3=$6; DEVSPEC=$7
	log "==============================================================="
	log "### $LB  시도 $TR"
	log "==============================================================="
	kill_app

	jq --argjson c0 "$C0" --argjson c1 "$C1" --argjson c2 "$C2" --argjson c3 "$C3" '
	      .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=120
	    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
	    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
	    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
	    | .VHL_CAM.i2c2.ch0.led_flash.enable=false | .VHL_CAM.i2c2.ch1.led_flash.enable=false
	    | .VHL_CAM.i2c1.ch2.led_flash.enable=false | .VHL_CAM.i2c1.ch3.led_flash.enable=false
	    ' "$BACKUP" >"$OUT/.tp.json" || return 1
	cp "$OUT/.tp.json" "$CONF"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/transport_app_${LB}_${TR}.log"

	# 기준선: 앱 기동 **직전** IRQ 스냅샷
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	HINFP=()

	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	# 주소 판별 (기동 직후 1회, 응답 없으면 샘플 루프에서 재시도)
	declare -A ADDR
	for spec in $DEVSPEC; do ADDR[${spec%%=*}]=""; done

	{
		printf "  %-7s %8s %8s %8s %8s" "t_ms" "csi0_d" "csi0fps" "isi0_d" "isi0fps"
		printf " %8s %8s" "csi1_d" "isi1_d"
		for spec in $DEVSPEC; do printf " %9s %9s" "${spec%%=*}_dHINF" "${spec%%=*}_sfps"; done
		printf "\n"
		printf "  %s\n" "-------------------------------------------------------------------------------------------------"
	} | tee -a "$LOG"

	for i in $(seq 1 "$SAMPLES"); do
		TS=$(( $(now_ms) - T0 ))

		# --- IRQ 계층 ---
		declare -A IRQC
		while read -r k v; do IRQC[$k]=$v; done < <(irq_all)
		D_CSI0=$(( ${IRQC[32e50000.csi]:-0} - ${IRQP[32e50000.csi]:-0} ))
		D_ISI0=$(( ${IRQC[32e02000.isi]:-0} - ${IRQP[32e02000.isi]:-0} ))
		D_CSI1=$(( ${IRQC[32e40000.csi]:-0} - ${IRQP[32e40000.csi]:-0} ))
		D_ISI1=$(( ${IRQC[32e00000.isi]:-0} - ${IRQP[32e00000.isi]:-0} ))
		for k in "${!IRQC[@]}"; do IRQP[$k]=${IRQC[$k]}; done

		# --- AP1302 계층 ---
		ROW=""; CSVX=""
		for spec in $DEVSPEC; do
			NM=${spec%%=*}; BA=${spec#*=}; BUS=${BA%%:*}; CAND=${BA##*:}
			[ -z "${ADDR[$NM]}" ] && ADDR[$NM]=$(pick_addr "$BUS" "$CAND")
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

		# CSI2 는 프레임당 2회(FS+FE) 이므로 /2. 경과시간으로 나눠 fps 로 환산한다.
		CF=$(awk -v d="$D_CSI0" -v ms="$IVL_MS" 'BEGIN{printf "%.1f", d/2.0*1000.0/ms}')
		IF=$(awk -v d="$D_ISI0" -v ms="$IVL_MS" 'BEGIN{printf "%.1f", d*1000.0/ms}')

		printf "  %-7s %8s %8s %8s %8s %8s %8s%s\n" \
			"$TS" "$D_CSI0" "$CF" "$D_ISI0" "$IF" "$D_CSI1" "$D_ISI1" "$ROW" | tee -a "$LOG"
		echo "$LB,$TR,$TS,$D_CSI0,$CF,$D_ISI0,$IF,$D_CSI1,$D_ISI1${CSVX}" >>"$CSV"

		# 다음 샘플 시각까지 정렬 대기 (i2c 읽기 지연이 누적되지 않게)
		NEXT=$(( T0 + i * IVL_MS ))
		SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done

	log "  판별된 AP1302 주소: $(for spec in $DEVSPEC; do printf '%s=%s ' "${spec%%=*}" "${ADDR[${spec%%=*}]:--}"; done)"
	log "  앱 preroll/NO DATA:"
	grep -nE "NOT prerolled|NO DATA for" "$AL" 2>/dev/null | head -5 | sed 's/^/    /' | tee -a "$LOG"
	log "  앱 enc-stat 처음 10줄:"
	grep -oE "ch[0-9] enc-stat\[[0-9]+s\] in=[0-9]+\([0-9.]+fps\)" "$AL" 2>/dev/null | head -10 | sed 's/^/    /' | tee -a "$LOG"
	log "  앱 enc-stat 중 in>0 인 줄 수: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0)"
	log ""

	kill_app
	find /dev/shm -name '*.mp4*' -mmin -10 -delete 2>/dev/null
	sleep 2
}

# --- 실행 -------------------------------------------------------------------
# A: 듀얼와이드 ch0+ch1 (실패 조건). 듀얼 모드 AP1302 = 0x11/0x12, 단일이면 0x3c.
probe "FAIL_ch0ch1" 1 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c"
probe "FAIL_ch0ch1" 2 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c"
# B: 대조 ch0+ch2 (같은 fps, 토폴로지만 다름). 단일 모드라 두 버스 모두 0x3c.
probe "OK_ch0ch2" 1 true false true false "ch0=2:0x3c,0x11 ch2=1:0x3c,0x11"

log "=== 종료 $(date -Is) ==="
