#!/usr/bin/env bash
#
# probe-aeon-bisect.sh — 듀얼와이드 360p120 의 "ISP 는 도는데 CSI2 로 못 넘어감" 을 만드는
#                        운영 config 항목을 단일요인으로 분해한다.
#
# 배경 (freeze_20260907_074217, 12회)
#   같은 채널·같은 fps·같은 하드웨어인데 json 만 다르면 결과가 갈린다:
#     통제 config  ch0+ch1 -> CSI2 233(=116fps), ISI 117, AP1302 dHINF 117/118  (4계층 일치)
#     운영 config  ch0+ch1 -> CSI2  24(= 12fps), ISI   0, AP1302 dHINF 107/93   (ISP 만 돔)
#   운영이 4/4 로 10fps 대, 통제가 0/4. => 원인은 토폴로지도 fps 도 아닌 **config 항목**이다.
#   (동결은 이와 별개 현상으로 통제해도 A 1/4, B 3/4 에서 남는다. 여기서는 다루지 않는다.)
#
#   ch0/ch1 에서 운영과 통제가 실제로 다른 항목은 7 개다(exp_time 은 양쪽 2000 으로 동일):
#     ae_on ch1 true->false / ae_gain ch0 512->256 / bps ch0 8192->4096 /
#     awb ch1 d65->auto / hflip ch0 true->false / wiper ch0 16->32 / flash_delay 128->0
#
#   1 순위는 ae_on 이고 소스 근거가 있다 — max9296.c:2918
#     bool skip_exposure_seed = ch_ctrl->ae_on && fps > safe_max_fps;
#   360p 의 safe_max_fps 는 30 이므로 120fps + ae_on=true 면 드라이버가 노출 시드를 건너뛴다.
#   운영은 ch0=false / ch1=true 비대칭이라 듀얼 쌍의 두 ISP 가 다른 타이밍으로 돌 수 있다.
#
# 설계 (4 갈래 x ROUNDS 회, 교차 배치)
#   D1 통제 전체                        ae_on=(false,false)   기준선
#   D2 통제 + ae_on 운영 비대칭          ae_on=(false,true)    ae_on 비대칭만
#   D3 통제 + ae_on 양쪽 on              ae_on=(true,true)     AE 대칭 on
#   D4 운영 전체 - ae_on 만 통제          ae_on=(false,false)   나머지 6 항목 전체
#   D2 가 죽고 D3 이 살면 비대칭이 원인, 둘 다 죽으면 ae_on=true 자체(시드 스킵),
#   D4 가 죽으면 범인은 나머지 6 개 안에 있다.
#
# 판정 신호는 스트림 시작 직후 바로 나온다(115fps vs 10fps)므로 창은 짧게 잡는다.
#   동결 = 스트림 시작 후 해당 CSI 증분이 0 이 되고 **끝까지 0 인** 첫 시각.
#   회차마다 (스트림 시작 / 동결 시각 / 평균 fps / 고정된 센서fps) 를 남긴다.
#
# 사용법: SAMPLES=35 ROUNDS=3 ./probe-aeon-bisect.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-35}
ROUNDS=${ROUNDS:-3}
IVL_MS=${IVL_MS:-1000}
EXPCTL=${EXPCTL:-2000}
BPSCTL=${BPSCTL:-4096}
NPROC=$(nproc)

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/aeon_$STAMP.log"
CSV="$OUT/aeon_$STAMP.csv"
SUM="$OUT/aeon_${STAMP}_summary.csv"
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
	for _ in $(seq 1 15); do pgrep -x capapp >/dev/null || return 0; sleep 1; done
	pkill -9 -x capapp 2>/dev/null
	sleep 2
}

assert_daemon_off() {
	if systemctl is-active --quiet cam-operate.service; then
		systemctl stop cam-operate.service >>"$LOG" 2>&1
		sleep 5
	fi
	pgrep -x gstApp >/dev/null 2>&1 && { pkill -x gstApp 2>/dev/null; sleep 3; }
	log "  데몬: cam-operate=$(systemctl is-active cam-operate.service) gstApp잔존=$(pgrep -cx gstApp 2>/dev/null || echo 0)"
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
	log ""
	log "### 최종 요약 ($SUM)"
	column -s, -t "$SUM" 2>/dev/null | sed 's/^/  /' | tee -a "$LOG" || cat "$SUM" | tee -a "$LOG"
	log "### 로그: $LOG"
	exit $rc
}
trap restore EXIT INT TERM

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

declare -A IRQP
declare -A HINFP
declare -A ADDR

sample_loop() { # $1=라벨 $2=trial $3=DEVSPEC $4=T0(ms)
	SL_LB=$1; SL_TR=$2; SL_DEVS=$3; SL_T0=$4
	HINFP=(); ADDR=()
	for spec in $SL_DEVS; do ADDR[${spec%%=*}]=""; done

	for i in $(seq 1 "$SAMPLES"); do
		TS=$(( $(now_ms) - SL_T0 ))
		declare -A IRQC
		while read -r k v; do IRQC[$k]=$v; done < <(irq_all)
		D_CSI0=$(( ${IRQC[32e50000.csi]:-0} - ${IRQP[32e50000.csi]:-0} ))
		D_ISI0=$(( ${IRQC[32e02000.isi]:-0} - ${IRQP[32e02000.isi]:-0} ))
		D_CSI1=$(( ${IRQC[32e40000.csi]:-0} - ${IRQP[32e40000.csi]:-0} ))
		D_ISI1=$(( ${IRQC[32e00000.isi]:-0} - ${IRQP[32e00000.isi]:-0} ))
		for k in "${!IRQC[@]}"; do IRQP[$k]=${IRQC[$k]}; done

		CSVX=""
		for spec in $SL_DEVS; do
			NM=${spec%%=*}; BA=${spec#*=}; BUS=${BA%%:*}; CAND=${BA##*:}
			if [ -z "${ADDR[$NM]}" ] && [ "$TS" -ge 8000 ]; then
				ADDR[$NM]=$(pick_addr "$BUS" "$CAND")
			fi
			A=${ADDR[$NM]}
			if [ -z "$A" ]; then CSVX="$CSVX,,"; continue; fi
			CUR=$(h2d "$(ap_rd "$BUS" "$A" 0x00 0x02 2 | cut -c1-2)")
			TF=$(h2d "$(ap_rd "$BUS" "$A" 0x00 0xfc 4)")
			SF=$(awk -v v="${TF:-0}" 'BEGIN{if(v>0) printf "%.1f", 1000000.0/v; else print ""}')
			P=${HINFP[$NM]:-}
			if [ -n "$P" ] && [ -n "$CUR" ]; then D=$(( (CUR - P + 256) % 256 )); else D=""; fi
			[ -n "$CUR" ] && HINFP[$NM]=$CUR
			CSVX="$CSVX,$D,$SF"
		done

		echo "$SL_LB,$SL_TR,$TS,$D_CSI0,,$D_ISI0,,$D_CSI1,$D_ISI1${CSVX}" >>"$CSV"

		NEXT=$(( SL_T0 + i * IVL_MS ))
		SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done
}

# 회차 요약: 스트림 시작 / 동결 시각 / 동결 전 평균 fps / 고정된 센서fps
summarize_run() { # $1=라벨 $2=trial $3=csi열 $4=isi열
	awk -F, -v K="$1" -v T="$2" -v CC="$3" -v IC="$4" -v SUMF="$SUM" -v IVL="$IVL_MS" '
		$1==K && $2==T {
			n++; t[n]=$3; c[n]=$CC+0; i[n]=$IC+0; sa[n]=$11; sb[n]=$13
		}
		END {
			start=-1; for (k=1;k<=n;k++) if (c[k]>0) { start=t[k]; si=k; break }
			if (start<0) { printf "%s,%s,NO_STREAM,,,,\n", K, T >> SUMF;
			               printf "  요약: 스트림 시작 안 됨\n"; exit }
			frz=-1
			for (k=si; k<=n; k++) {
				if (c[k]==0) { ok=1; for (m=k;m<=n;m++) if (c[m]>0) { ok=0; break }
				               if (ok) { frz=t[k]; fi=k; break } }
			}
			last = (frz<0 ? n : fi-1)
			s=0; cnt=0
			for (k=si; k<=last; k++) { s+=c[k]; cnt++ }
			fps = (cnt>0 ? s/cnt/2.0*1000.0/IVL : 0)
			if (frz<0) {
				printf "%s,%s,완주,%d,,%.1f,\n", K, T, start, fps >> SUMF
				printf "  요약: 스트림시작 %dms / 동결 없음(%d초 완주) / 평균 %.1f fps\n",
				       start, t[n]/1000, fps
			} else {
				printf "%s,%s,동결,%d,%d,%.1f,%s|%s\n", K, T, start, frz, fps, sa[n], sb[n] >> SUMF
				printf "  요약: 스트림시작 %dms / **동결 %dms** / 동결전 평균 %.1f fps / 고정 센서fps %s·%s\n",
				       start, frz, fps, sa[n], sb[n]
			}
		}' "$CSV" | tee -a "$LOG"
}

probe() { # $1=라벨 $2=trial $3=mode(ctl|prod) $4=ae_on_ch0 $5=ae_on_ch1
	LB=$1; TR=$2; MODE=$3; AE0=$4; AE1=$5
	C0=true; C1=true; C2=false; C3=false          # 항상 ch0+ch1 듀얼와이드
	DEVS="ch0=2:0x11,0x3c ch1=2:0x12,0x3c"; CC=4; IC=6
	log "==============================================================="
	log "### $LB  시도 $TR  (mode=$MODE, ae_on=($AE0,$AE1), 120fps, 창 ${SAMPLES}s)"
	log "==============================================================="
	kill_cap
	assert_daemon_off

	if [ "$MODE" = "ctl" ]; then
		# enable 외 전 항목을 4채널 동일값으로 누른다.
		jq --argjson e "$EXPCTL" --argjson b "$BPSCTL" --argjson c0 "$C0" --argjson c1 "$C1" \
		   --argjson c2 "$C2" --argjson c3 "$C3" '
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
		    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
		    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
		    ' "$BACKUP" >"$OUT/.fz.json" || return 1
	else
		# 운영값 그대로. led_flash 만 4채널 off (선행 실패가 관측된 조건과 동일).
		jq --argjson c0 "$C0" --argjson c1 "$C1" --argjson c2 "$C2" --argjson c3 "$C3" '
		      .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=120
		    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
		    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
		    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
		    | .VHL_CAM.i2c2.ch0.led_flash.enable=false | .VHL_CAM.i2c2.ch1.led_flash.enable=false
		    | .VHL_CAM.i2c1.ch2.led_flash.enable=false | .VHL_CAM.i2c1.ch3.led_flash.enable=false
		    ' "$BACKUP" >"$OUT/.fz.json" || return 1
	fi
	# ae_on 은 이 시험의 독립변수다 — 두 모드 모두 마지막에 덮어쓴다.
	jq --argjson a0 "$AE0" --argjson a1 "$AE1" \
	   '.VHL_CAM.i2c2.ch0.ae_on=$a0 | .VHL_CAM.i2c2.ch1.ae_on=$a1' \
	   "$OUT/.fz.json" >"$OUT/.ae.json" || return 1
	cp "$OUT/.ae.json" "$CONF"
	CONF_DIRTY=1

	# ctl 모드는 enable·ae_on 을 뺀 나머지가 4채널 동일해야 한다(ae_on 은 독립변수라 제외).
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable, .ae_on)) | unique | length' "$CONF")
	if [ "$MODE" = "ctl" ] && [ "$UNIQ" != "1" ]; then
		log "  !!! 통제 검증 실패 (unique=$UNIQ) — 이 회차 무효 !!!"
	fi
	log "  config: mode=$MODE unique(enable·ae_on 제외)=$UNIQ exp=$(jq -r '[.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]|@csv' "$CONF")"
	log "  ch0/ch1 실제값: $(jq -c '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1]|map({ae_on,ae_gain,bps:.bps[0],awb,hflip,wiper:.led_flash.wiper,fd:.led_flash.flash_delay})' "$CONF")"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/aeon_app_${LB}_${TR}.log"
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  앱 enc-stat in>0 줄 수: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0) / preroll·NODATA $(grep -cE "NOT prerolled|NO DATA for" "$AL" 2>/dev/null || echo 0)건"
	summarize_run "$LB" "$TR" "$CC" "$IC"
	log ""

	kill_cap
	find /dev/shm -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
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

log "=== ae_on 단일요인 분해 $(date -Is) ==="
log "창 ${SAMPLES}s x ${ROUNDS}라운드, 4갈래 교차 배치. 항상 ch0+ch1 듀얼와이드 120fps. 통제 exp=$EXPCTL bps=$BPSCTL."
log ""
echo "case,trial,t_ms,csi0_d,,isi0_d,,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"
echo "case,trial,result,start_ms,freeze_ms,fps_before,sensor_fps_frozen" >"$SUM"

for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	probe "D1_ctl_ae_ff"   "$r" ctl  false false   # 기준선
	probe "D2_ctl_ae_ft"   "$r" ctl  false true    # ae_on 비대칭(운영과 동일)
	probe "D3_ctl_ae_tt"   "$r" ctl  true  true    # AE 대칭 on
	probe "D4_prod_ae_ff"  "$r" prod false false   # 나머지 6항목 전체
done

log "=== 종료 $(date -Is) ==="
