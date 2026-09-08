#!/usr/bin/env bash
#
# probe-exposure-controlled.sh — 노출·AE 를 통제한 뒤 듀얼와이드 360p120 실패를 재판별한다.
#
# 왜 다시 재는가 — 선행 실행(dualtopo_20260907_070933)의 설계 결함
# ------------------------------------------------------------------
# 선행 실행은 두 존재증명을 냈다(이건 교란요인과 무관하게 유효하다):
#   - ch2+ch3 듀얼와이드 @120 이 앱까지 정상(105fps ISI, 앱 113~115) 2/2
#     => "듀얼와이드 x 120fps 는 구조적으로 불가" 라는 일반화는 반증됐다.
#   - ch0+ch1 독립 파이프라인 @120 이 82~90fps 로 실제로 흐른다
#     => "isi_1 라인버퍼/대역 구조 한계" 설명도 반증됐다.
# 그러나 **인과는 못 짚는다**. 운영 config 가 버스별로 다르기 때문이다:
#     i2c2(ch0/ch1) exp_time=2000, ae_on ch0=false ch1=true, ae_gain ch0=512
#     i2c1(ch2/ch3) exp_time=50000, ae_on ch2=true ch3=false
#   즉 실험 A 는 "인스턴스"와 "노출 2000 vs 50000"이 함께 바뀌었고,
#      실험 B 는 "캡처 경로"와 "노출/AE(운영값 vs 리셋 후 기본값)"가 함께 바뀌었다.
#   이 장비는 R0x1186 SYNC_MODE=2 (노출 중심 트리거)라 노출이 프레임 타이밍을 좌우한다는 것이
#   이미 실측돼 있으므로, 노출을 통제하지 않은 비교로는 어느 쪽도 주장할 수 없다.
#
# 통제 조건 (전 회차 공통)
#   4채널 ae_on=false, ae_gain=256, led_flash.enable=false, flash_delay=0,
#   양 버스 exp_time 동일값. 적용 후 v4l2 컨트롤을 **되읽어 검증**한다.
#   ae_on=false 는 노출을 독립변수로 만들기 위한 기전 규명용 조건이지 기준 조건이 아니다.
#
# 격자 (5회차)
#   S1 ch0+ch1 앱 @120 exp=2000   <- 실패 재현 기준선(통제 조건)
#   S2 ch0+ch1 앱 @120 exp=4000
#   S3 ch0+ch1 앱 @120 exp=6000   <- S1~S3 로 노출 효과
#   S4 ch2+ch3 앱 @120 exp=2000   <- S1 과 **인스턴스만** 다름
#   S5 ch0+ch1 독립 @120 exp=2000 <- S1 과 **캡처 경로만** 다름
#   120fps 의 프레임 주기는 8,333us 이므로 스윕 상한을 6,000 으로 둔다.
#
# 주의
#   - 독립 파이프라인은 cam-operate 데몬이 꺼져 있어야 한다(회차마다 단언).
#   - 360p@120 에서 **스트리밍 중** v4l2 노출 변경은 파이프라인을 죽인다. 컨트롤은 STREAMON
#     **이전에만** 쓴다.
#   - 독립 경로는 prepare 를 직접 써서 state=CONSUMED/READY 를 확인한 뒤 캡처한다.
#     (enable+media-ctl 만 쓰는 폐기된 raw 방식은 20~25% 낮게 나온다.)
#
# 사용법: SAMPLES=40 ./probe-exposure-controlled.sh
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
LOG="$OUT/expctl_$STAMP.log"
CSV="$OUT/expctl_$STAMP.csv"
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

pick_addr() {
	b=$1
	for a in $(echo "$2" | tr ',' ' '); do
		[ -n "$(ap_rd "$b" "$a" 0x00 0x02 2)" ] && { echo "$a"; return 0; }
	done
	echo ""
}

# 컨트롤 되읽기 — 통제가 실제로 걸렸는지 확인한다(값을 가정하지 않는다).
read_ctrls() { # $1=subdev번호 $2=chA $3=chB
	v4l2-ctl -d "/dev/v4l-subdev$1" \
		--get-ctrl="ae_on_ch$2,ae_on_ch$3,exp_time" 2>&1 | tr '\n' ' '
}

# 독립 경로 전용: 컨트롤을 STREAMON 이전에 쓴다.
apply_ctrls() { # $1=subdev번호 $2=chA $3=chB $4=exp
	for c in "ae_on_ch$2=0" "ae_on_ch$3=0" "exp_time=$4"; do
		R=$(v4l2-ctl -d "/dev/v4l-subdev$1" --set-ctrl="$c" 2>&1)
		[ -n "$R" ] && log "    set-ctrl $c -> $R"
	done
	# auto_gain/gain 은 subdev 에 따라 없을 수 있다. 있으면 맞추고, 없으면 조용히 넘어간다.
	for c in "auto_gain_ch$2=0" "auto_gain_ch$3=0" "gain_ch$2=256" "gain_ch$3=256"; do
		v4l2-ctl -d "/dev/v4l-subdev$1" --set-ctrl="$c" >/dev/null 2>&1
	done
}

declare -A IRQP
declare -A HINFP
declare -A ADDR

sample_loop() { # $1=라벨 $2=trial $3=DEVSPEC $4=T0(ms)
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
			# 듀얼 주소(0x11/0x12)는 펌웨어 로드 후에야 응답한다. t>=8s 전에는 확정하지 않는다.
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

# --- 앱 경유 ----------------------------------------------------------------
probe_app() { # $1=라벨 $2=trial $3=exp $4..$7=ch0..ch3 $8=DEVSPEC $9=subdev $10=chA $11=chB
	LB=$1; TR=$2; EXP=$3; C0=$4; C1=$5; C2=$6; C3=$7; DEVS=$8; SD=$9; CA=${10}; CB=${11}
	banner "$LB  시도 $TR  (앱 경유, 120fps, exp=$EXP, bps=$BPSCUR, ae_on=false 통일)"
	kill_cap
	assert_daemon_off

	# 운영 config 는 채널마다 값이 다르다(전수 확인: bps 8192/4096/2048/1024, awb auto/d65/off/d75,
	# vflip·hflip 제각각, ae_on·ae_gain 혼합, wiper 16/32/48/63, 버스 exp_time 2000 vs 50000).
	# **채널 간에 다른 항목은 하나도 남기지 않고** 전부 같은 값으로 눌러야 비교가 성립한다.
	# 특히 bps 는 ch0+ch1 이 12,288kbps, ch2+ch3 가 3,072kbps 로 4배 차이라 앱 경로의 인코더
	# 부하를 통째로 바꾼다 — 이걸 안 눌러서 선행 실행이 교란됐다.
	# enable 만 독립변수로 남긴다.
	jq --argjson e "$EXP" --argjson b "$BPSCUR" --argjson c0 "$C0" --argjson c1 "$C1" \
	   --argjson c2 "$C2" --argjson c3 "$C3" '
	      def ctl:
	          .enable_keep = .enable
	        | .vflip = false | .hflip = false
	        | .ae_on = false | .ae_gain = 256
	        | .bps = [$b, $b]
	        | .awb = "auto"
	        | .led_flash.enable = false
	        | .led_flash.wiper = 32
	        | .led_flash.flash_delay = 0
	        | .gop = [30, 15] | .profile = [0, 0] | .quant = [-1, -1]
	        | .qp_min = [0, 0] | .qp_max = [0, 0]
	        | .dz_x = 32768 | .dz_y = 32768
	        | del(.enable_keep);
	      .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=120
	    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
	    | .VHL_CAM.i2c2.exp_time=$e | .VHL_CAM.i2c1.exp_time=$e
	    | .VHL_CAM.i2c2.dz=100 | .VHL_CAM.i2c1.dz=100
	    | .VHL_CAM.i2c2.crop_enable=false | .VHL_CAM.i2c1.crop_enable=false
	    | .VHL_CAM.i2c2.ch0 |= ctl | .VHL_CAM.i2c2.ch1 |= ctl
	    | .VHL_CAM.i2c1.ch2 |= ctl | .VHL_CAM.i2c1.ch3 |= ctl
	    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
	    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
	    ' "$BACKUP" >"$OUT/.ec.json" || return 1
	# cp 도중 죽어도 복원되도록 쓰기 "전"에 세운다
	CONF_DIRTY=1
	cp "$OUT/.ec.json" "$CONF"

	# 통제가 실제로 걸렸는지 config 를 되읽어 남긴다 (가정하지 않는다).
	log "  통제 확인 exp_time: i2c2=$(jq -r '.VHL_CAM.i2c2.exp_time' "$CONF") i2c1=$(jq -r '.VHL_CAM.i2c1.exp_time' "$CONF")"
	log "  채널값: $(jq -c '[.VHL_CAM.i2c2.ch0, .VHL_CAM.i2c2.ch1, .VHL_CAM.i2c1.ch2, .VHL_CAM.i2c1.ch3]
	                       | map({enable,ae_on,ae_gain,bps,awb,vflip,hflip,led:.led_flash.enable})' "$CONF")"
	# enable 을 뺀 나머지가 4채널 모두 같아야 비교가 성립한다. 다르면 크게 남긴다.
	DIFFN=$(jq -r '[.VHL_CAM.i2c2.ch0, .VHL_CAM.i2c2.ch1, .VHL_CAM.i2c1.ch2, .VHL_CAM.i2c1.ch3]
	               | map(del(.enable)) | unique | length' "$CONF" 2>/dev/null || echo "?")
	if [ "$DIFFN" = "1" ]; then
		log "  통제 검증: 4채널이 enable 을 뺀 전 항목에서 동일 (unique=1) — OK"
	else
		log "  !!! 통제 검증 실패: 4채널이 여전히 다르다 (unique=$DIFFN) — 이 회차 비교는 무효 !!!"
	fi

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/expctl_app_${LB}_${TR}.log"
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  컨트롤 되읽기(subdev$SD): $(read_ctrls "$SD" "$CA" "$CB")"
	log "  prepare: $(cat "/sys/bus/i2c/devices/$([ "$SD" = 2 ] && echo 2-0048 || echo 1-0048)/prepare" 2>/dev/null)"
	log "  앱 preroll/NO DATA: $(grep -cE "NOT prerolled|NO DATA for" "$AL" 2>/dev/null || echo 0)건"
	log "  앱 enc-stat in>0 줄 수: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0)"
	log "  앱 enc-stat 마지막: $(grep -oE 'ch[0-9] enc-stat\[[0-9]+s\] in=[0-9]+\([0-9.]+fps\)' "$AL" 2>/dev/null | tail -2 | tr '\n' ' ')"
	log ""
	kill_cap
	find /dev/shm -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
	sleep 2
}

# --- 독립 파이프라인 --------------------------------------------------------
probe_raw() { # $1=라벨 $2=trial $3=exp $4=csi(0|1) $5=DEVSPEC $6=subdev $7=chA $8=chB
	LB=$1; TR=$2; EXP=$3; CSI=$4; DEVS=$5; SD=$6; CA=$7; CB=$8
	banner "$LB  시도 $TR  (독립 파이프라인, 120fps, exp=$EXP, ae_on=false 통일)"
	kill_cap
	assert_daemon_off

	case "$CSI" in
	0) DEV=2-0048; VID=/dev/video4 ;;
	1) DEV=1-0048; VID=/dev/video3 ;;
	*) log "  잘못된 csi=$CSI"; return 1 ;;
	esac
	W=1280; H=360; EN=3; FPS=120

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
	*) log "  !! prepare 가 CONSUMED/READY 에 못 감 — 이 회차는 무효"; return 1 ;;
	esac

	# 스트리밍 전에만 컨트롤을 쓴다 (360p@120 은 스트리밍 중 노출 변경으로 죽는다).
	apply_ctrls "$SD" "$CA" "$CB" "$EXP"
	log "  컨트롤 적용 후 되읽기(subdev$SD): $(read_ctrls "$SD" "$CA" "$CB")"

	MC=$(media-ctl -V "\"max9296 $SD\":0 [fmt:UYVY8_2X8/${W}x${H}@1/${FPS}]" 2>&1)
	[ -n "$MC" ] && log "  media-ctl: $MC"

	RL="$OUT/expctl_raw_${LB}_${TR}.log"
	CNT=$(( (SAMPLES + 25) * FPS ))
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid timeout $((SAMPLES + 30)) v4l2-ctl -d "$VID" \
		--set-fmt-video=width=${W},height=${H},pixelformat=RGBP \
		--stream-mmap --stream-count="$CNT" </dev/null >"$RL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  컨트롤 최종 되읽기(subdev$SD): $(read_ctrls "$SD" "$CA" "$CB")"
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

log "=== 노출 통제 재판별 $(date -Is) ==="
log "통제: 4채널 ae_on=false, ae_gain=256, led off, flash_delay=0, 양 버스 exp_time 동일."
log "샘플 ${SAMPLES}회 x ${IVL_MS}ms. AP1302 주소는 t>=8s 에 판별."
log "설정 백업 md5=$ORIG_MD5 / 시험 전 live md5=$LIVE_MD5"
log ""

echo "case,trial,t_ms,csi0_d,csi0_fps,isi0_d,isi0_fps,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"

# 노출 스윕 (ch0+ch1 듀얼, 앱). bps 는 전 회차 4096 고정 — S1~S4 사이 유일한 차이는 각 라벨의 변수뿐.
BPSCUR=4096
probe_app "S1_ch0ch1_exp2000" 1 2000 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 2 0 1
probe_app "S2_ch0ch1_exp4000" 1 4000 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 2 0 1
probe_app "S3_ch0ch1_exp6000" 1 6000 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 2 0 1
# 인스턴스만 다른 대조 (S1 과 enable 채널만 다름)
probe_app "S4_ch2ch3_exp2000" 1 2000 false false true true "ch2=1:0x11,0x3c ch3=1:0x12,0x3c" 3 2 3
# 인코더 부하만 다른 대조 (S1 과 bps 만 다름). 운영은 ch0+ch1 이 12,288kbps 로 ch2+ch3(3,072)의 4배다.
BPSCUR=1024
probe_app "S6_ch0ch1_exp2000_bps1024" 1 2000 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 2 0 1
BPSCUR=4096
# 캡처 경로만 다른 대조 (앱 없음 = 인코더도 없음. S1 과 함께 읽어야 한다.)
probe_raw "S5_raw_ch0ch1_exp2000" 1 2000 0 "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 2 0 1

log "=== 종료 $(date -Is) ==="
