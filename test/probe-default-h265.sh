#!/usr/bin/env bash
#
# probe-default-h265.sh — **패키지 정본 기본 운영 설정**으로 듀얼와이드 360p120 을 검증한다.
#
# 왜 다시 재는가
#   앞선 측정들은 보드의 /root/shared_v/edgeconf_pim.json 을 기준으로 삼았는데, 그 파일은
#   현장에서 채널별로 손댄 상태였다(ae_on 혼합, exp 2000/50000, bps 8192/4096, ae_gain 512/256).
#   정본은 패키지가 배포하는 edgeconf_pim_base.json 이고 배포 사슬은
#     /opt/pim/config/edgeconf_pim_base.json -> /etc/defaultconf.json -> /root/shared_v/edgeconf_pim.json
#     (dist/pim/DEBIAN/postinst:391,404 / factory_init_pim_gate.sh:19)
#   정본은 **4채널 전부 ae_on=true, ae_gain=256, bps=2048, 양 버스 exp_time=10000** 으로 대칭이고,
#   기본 운영 설정은 여기에 enc="h265" 만 적용한 것이다(정본 enc=null).
#
# 확정된 근인 (aeon_20260907_081557 + ch23_20260907_083448, 통제 시행 21회)
#   ae_on 이 듀얼 쌍의 두 채널에서 갈리고 시드 노출이 짧으면 6/6 고장(약 10fps).
#   대칭이면 값 무관 정상. 비대칭이어도 시드가 프레임주기를 넘으면(exp=50000) 정상.
#   기전: max9296.c:2918 skip_exposure_seed = ae_on && fps > safe_max_fps (safe_max_fps=30, :59)
#   => 정본은 ae_on 대칭 + exp=10000(120fps 주기 8,333us 초과)이라 양쪽 안전조건을 모두 만족한다.
#      예측: 정상. 이 예측을 실제로 확인한다.
#
# 설계 (창 90s)
#   F1 정본+h265 @120 ch0+ch1 듀얼와이드  x ROUNDS
#   F2 정본+h265 @120 ch2+ch3 듀얼와이드  x ROUNDS   (교차 배치)
#   F3 정본+h265 @30  ch0+ch1            x 1        (실배포 fps 대조)
#   정본에서 바꾸는 것은 enc·fps·enable·계측용 로그 설정뿐이다. 카메라/인코더 파라미터는 손대지 않는다.
#   4채널 전부 @120 은 fps 예산 상한(240)을 넘으므로 2채널로 돌린다.
#
# 판정: 동결 = 스트림 시작 후 해당 CSI 증분이 0 이 되고 끝까지 0 인 첫 시각.
#
# 사용법: SAMPLES=90 ROUNDS=3 ./probe-default-h265.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"           # 복원용: 보드의 현재(현장수정) 설정
BASEDEF=${BASEDEF:-/etc/defaultconf.json}  # 시험 기준: 패키지 정본
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-90}
ROUNDS=${ROUNDS:-4}
IVL_MS=${IVL_MS:-1000}
EXPCTL=${EXPCTL:-2000}
BPSCTL=${BPSCTL:-4096}
NPROC=$(nproc)

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/dflt_$STAMP.log"
CSV="$OUT/dflt_$STAMP.csv"
SUM="$OUT/dflt_${STAMP}_summary.csv"
log() { echo "$*" | tee -a "$LOG"; }

[ -e "$BACKUP" ] || { echo "백업 없음: $BACKUP"; exit 2; }
[ -e "$BASEDEF" ] || { echo "정본 없음: $BASEDEF"; exit 2; }
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

probe() { # $1=라벨 $2=trial $3=fps $4..$7=ch0..3 $8=DEVSPEC $9=csi열 $10=isi열
	LB=$1; TR=$2; FPS=$3; C0=$4; C1=$5; C2=$6; C3=$7; DEVS=$8; CC=$9; IC=${10}
	log "==============================================================="
	log "### $LB  시도 $TR  (정본+h265, ${FPS}fps, 창 ${SAMPLES}s)"
	log "==============================================================="
	kill_cap
	assert_daemon_off

	# 정본에서 바꾸는 것은 enc/fps/enable/계측용 로그 설정뿐. 카메라·인코더 파라미터는 손대지 않는다.
	jq --argjson f "$FPS" --argjson c0 "$C0" --argjson c1 "$C1" \
	   --argjson c2 "$C2" --argjson c3 "$C3" '
	      .VHL_CAM.enc="h265"
	    | .VHL_CAM.cam_width=640 | .VHL_CAM.cam_height=360 | .VHL_CAM.fps=$f
	    | .VHL_CAM.debug_level=5 | .VHL_CAM.queue_tune.enc_stat_sec=1
	    | .VHL_CAM.i2c2.ch0.enable=$c0 | .VHL_CAM.i2c2.ch1.enable=$c1
	    | .VHL_CAM.i2c1.ch2.enable=$c2 | .VHL_CAM.i2c1.ch3.enable=$c3
	    ' "$BASEDEF" >"$OUT/.fz.json" || return 1
	cp "$OUT/.fz.json" "$CONF"
	CONF_DIRTY=1

	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	log "  정본 md5=$BASEMD5 / 채널unique(enable 제외)=$UNIQ"
	log "  실제값: enc=$(jq -r '.VHL_CAM.enc' "$CONF") fps=$(jq -r '.VHL_CAM.fps' "$CONF") exp=$(jq -r '[.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]|@csv' "$CONF")"
	log "  채널: $(jq -c '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]|map({en:.enable,ae_on,ae_gain,bps:.bps[0],led:.led_flash.enable})' "$CONF")"
	[ "$UNIQ" = "1" ] || log "  !!! 정본이 4채널 동일이 아니다 (unique=$UNIQ) — 확인 필요 !!!"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/dflt_app_${LB}_${TR}.log"
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

BASEMD5=$(md5sum "$BASEDEF" | awk '{print $1}')
log "=== 패키지 정본 기본 운영 설정(+h265) 검증 $(date -Is) ==="
log "창 ${SAMPLES}s x ${ROUNDS}라운드. 기준 정본=$BASEDEF (md5 $BASEMD5)"
log ""
echo "case,trial,t_ms,csi0_d,,isi0_d,,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"
echo "case,trial,result,start_ms,freeze_ms,fps_before,sensor_fps_frozen" >"$SUM"

for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	probe "F1_ch0ch1_120" "$r" 120 true  true  false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 4 6
	probe "F2_ch2ch3_120" "$r" 120 false false true  true  "ch2=1:0x11,0x3c ch3=1:0x12,0x3c" 8 9
done
# 실배포 fps 대조 1회
probe "F3_ch0ch1_030" 1 30 true true false false "ch0=2:0x11,0x3c ch1=2:0x12,0x3c" 4 6

log "=== 종료 $(date -Is) ==="
