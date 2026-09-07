#!/usr/bin/env bash
#
# probe-aeon-ch23.sh — ae_on 비대칭 법칙의 반례(ch2+ch3)를 검증한다.
#
# 확정된 것 (aeon_20260907_081557, 12회, ch0+ch1 듀얼와이드 640x360@120)
#   ae_on 외 모든 항목을 통제했을 때:
#     (false,false) 정상 92.3fps / (true,true) 정상 100.9fps / **(false,true) 3/3 고장 10.3fps**
#     운영값 그대로 두고 ae_on 만 대칭화 -> 정상 111.6fps (나머지 6항목 무죄)
#   기전: max9296.c:2918  skip_exposure_seed = ch_ctrl->ae_on && fps > safe_max_fps
#         safe_max_fps=30 (max9296.c:59, 360p 모드가 이 값 사용 :919 :929)
#         => fps>30 이면 ae_on=true 인 채널만 노출 시드를 건너뛴다. 듀얼 쌍의 두 ISP 가
#            서로 다른 노출/타이밍으로 돌아 합성 와이드 프레임이 성립하지 않는다.
#         fps=30 에서는 30>30 이 거짓이라 양 채널 모두 시드를 받는다(대칭) -> 운영은 멀쩡했다.
#
# 반례
#   운영 i2c1 도 ch2=true / ch3=false 로 **비대칭**인데, 통제 전 실행에서 ch2+ch3@120 이
#   105fps 로 정상이었다. 다만 그쪽은 exp_time=50000 (i2c2 는 2000) 이라 시드가 프레임주기
#   8,333us 를 넘는다 — 두 채널의 실효 노출 차이가 작아졌을 가능성이 있으나 미검증이다.
#
# 설계 (3 갈래 x ROUNDS 회, 교차 배치. 항상 ch2+ch3 듀얼와이드 640x360@120)
#   E1 exp=2000  ae_on=(false,false)  대칭·짧은 노출 — 이 경로 자체가 도는지 기준선
#   E2 exp=2000  ae_on=(true,false)   비대칭(운영 i2c1 방향) + 짧은 노출
#   E3 exp=50000 ae_on=(true,false)   비대칭 + 운영 i2c1 노출 (원래 정상이던 조건)
#   E2 가 고장이면 비대칭 법칙은 인스턴스 무관하게 성립한다.
#   E3 이 정상이면 긴 노출이 비대칭을 무해화한 것이고, 반례가 설명된다.
#   ae_on 외 모든 항목은 4채널 동일값으로 누르고 회차마다 unique=1 로 검증한다.
#
# 판정: 동결 = 스트림 시작 후 해당 CSI 증분이 0 이 되고 끝까지 0 인 첫 시각.
#       판별 신호는 평균 fps (정상 ~110 vs 고장 ~10) 로 스트림 시작 직후 바로 나온다.
#
# 사용법: SAMPLES=35 ROUNDS=3 ./probe-aeon-ch23.sh
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
LOG="$OUT/ch23_$STAMP.log"
CSV="$OUT/ch23_$STAMP.csv"
SUM="$OUT/ch23_${STAMP}_summary.csv"
log() { echo "$*" | tee -a "$LOG"; }

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

probe() { # $1=라벨 $2=trial $3=mode(ctl|prod) $4=ae_on_chA $5=ae_on_chB $6=exp_time
	LB=$1; TR=$2; MODE=$3; AE0=$4; AE1=$5; EXP=$6
	C0=false; C1=false; C2=true; C3=true          # 항상 ch2+ch3 듀얼와이드
	DEVS="ch2=1:0x11,0x3c ch3=1:0x12,0x3c"; CC=8; IC=9
	log "==============================================================="
	log "### $LB  시도 $TR  (mode=$MODE, ch2/ch3 ae_on=($AE0,$AE1), exp=$EXP, 120fps, 창 ${SAMPLES}s)"
	log "==============================================================="
	kill_cap
	assert_daemon_off

	if [ "$MODE" = "ctl" ]; then
		# enable 외 전 항목을 4채널 동일값으로 누른다.
		jq --argjson e "$EXP" --argjson b "$BPSCTL" --argjson c0 "$C0" --argjson c1 "$C1" \
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
	   '.VHL_CAM.i2c1.ch2.ae_on=$a0 | .VHL_CAM.i2c1.ch3.ae_on=$a1' \
	   "$OUT/.fz.json" >"$OUT/.ae.json" || return 1
	cp "$OUT/.ae.json" "$CONF"

	# ctl 모드는 enable·ae_on 을 뺀 나머지가 4채널 동일해야 한다(ae_on 은 독립변수라 제외).
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable, .ae_on)) | unique | length' "$CONF")
	if [ "$MODE" = "ctl" ] && [ "$UNIQ" != "1" ]; then
		log "  !!! 통제 검증 실패 (unique=$UNIQ) — 이 회차 무효 !!!"
	fi
	log "  config: mode=$MODE unique(enable·ae_on 제외)=$UNIQ exp=$(jq -r '[.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]|@csv' "$CONF")"
	log "  ch2/ch3 실제값: $(jq -c '[.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]|map({ae_on,ae_gain,bps:.bps[0],awb,hflip,wiper:.led_flash.wiper,fd:.led_flash.flash_delay})' "$CONF")"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/ch23_app_${LB}_${TR}.log"
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	sample_loop "$LB" "$TR" "$DEVS" "$T0"

	log "  앱 enc-stat in>0 줄 수: $(grep -cE "enc-stat\[[0-9]+s\] in=[1-9]" "$AL" 2>/dev/null || echo 0) / preroll·NODATA $(grep -cE "NOT prerolled|NO DATA for" "$AL" 2>/dev/null || echo 0)건"
	summarize_run "$LB" "$TR" "$CC" "$IC"
	log ""

	kill_cap
	find /dev/shm -name '*.mp4*' -mmin -10 -delete 2>/dev/null
	sleep 2
}

# --- 준비 -------------------------------------------------------------------
if systemctl is-active --quiet cam-operate.service; then
	systemctl stop cam-operate.service >>"$LOG" 2>&1
	sleep 5
fi
pkill -x killcam 2>/dev/null
sleep 2
cp /usr/local/bin/gstApp "$BIN" && chmod +x "$BIN"

log "=== ch2+ch3 반례 검증: ae_on 비대칭 x 노출 $(date -Is) ==="
log "창 ${SAMPLES}s x ${ROUNDS}라운드, 3갈래 교차 배치. 항상 ch2+ch3 듀얼와이드 120fps. bps=$BPSCTL, 노출은 갈래별."
log ""
echo "case,trial,t_ms,csi0_d,,isi0_d,,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"
echo "case,trial,result,start_ms,freeze_ms,fps_before,sensor_fps_frozen" >"$SUM"

for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	probe "E1_exp2000_ae_ff" "$r" ctl false false 2000   # 대칭, 짧은 노출 — 기준선
	probe "E2_exp2000_ae_tf" "$r" ctl true  false 2000   # 비대칭(운영 i2c1 방향) + 짧은 노출
	probe "E3_exp50000_ae_tf" "$r" ctl true false 50000  # 비대칭 + 운영 i2c1 노출(원래 정상이던 조건)
done

log "=== 종료 $(date -Is) ==="
