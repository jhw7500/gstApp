#!/usr/bin/env bash
#
# probe-exposure-split.sh — 듀얼와이드 360p120 고장의 근인이 "시드 스킵의 200ms 스큐" 인가
#                           "쌍의 정상상태 노출 발산" 인가를 가른다.
#
# 왜 필요한가 — 기존 모델이 자기 실측으로 반증된다
#   #64 는 `ae_on` 비대칭이 고장을 만든다고 기록했다(비대칭+짧은시드 6/6 고장).
#   그때 함께 나온 네 번째 점이 모델과 어긋난다 — **비대칭인데 시드가 길면(50000) 정상**(103.8fps).
#   max9296.c:2938-2952 의 시드 블록은 `msleep(100)` 2회가 고정이고 max9296_write_exposure()
#   는 정책 검사 뒤 고정 페이로드 i2c 쓰기 1회다(:2452-2458). 즉 시드 2000 과 50000 은
#   **바이트 하나만 다른 동일 시퀀스**이고 스큐도 같다. 스큐가 근인이면 두 결과가 갈릴 수 없다.
#   => 남는 가설: 근인은 시드 스킵 자체가 아니라 **두 채널의 정상상태 노출이 갈리는 것**이다.
#      SYNC_MODE=2(노출 중심 트리거)에서 노출 차이가 곧 프레임 타이밍 차이가 된다.
#
# 설계 — 노출만 남기고 전부 지운다
#   4채널 전부 `ae_on=false` 로 **대칭**이므로 시드 스킵이 양쪽 다 없다(스큐 0).
#   `ae_on` 비대칭도 없다. 스트림이 정상으로 돈 뒤 **런타임에 한쪽 노출만** 바꾼다.
#   드라이버가 채널별 쓰기를 지원한다 — V4L2_CID_EXPOSURE_CH0/CH1(max9296.c:179-180),
#   적용부 :3283/:3346 이 각 채널 AP1302 에 직접 쓴다.
#
#   구간 3개 (한 회차 안에서 전후 비교, 노출 외 모든 변수 고정)
#     P1  0    ~ T1   개입 없음                      기준선
#     P2  T1   ~ T2   exp_time_ch1 <- $VAL           판별 지점
#     P3  T2   ~ 끝   exp_time_ch1 <- $EXPCTL 복귀   복구 확인
#
#   갈래 4개 = 2x2 (교차 배치). 120fps 의 frame_period 는 8,333us 다.
#     S_sham    대칭   over=0   exp_time_ch1 <- 2000    v4l2 쓰기 **자체**의 교란 통제
#     X_split   비대칭 한쪽1    exp_time_ch1 <- 11000   노출 분리 + 주기 초과
#     Y_under   비대칭 양쪽0    exp_time_ch1 <- 4000    노출 "차이" 자체를 본다
#     Z_botover 대칭   양쪽1    exp_time    <- 11000   "주기 초과" 자체를 본다
#   Y 가 붕괴하면 술어는 "쌍의 노출 차이", Z 가 붕괴하면 "주기 초과",
#   Y 정상 + Z 정상이면 술어는 "비대칭 ^ 한쪽 주기 초과" 의 결합이다.
#
# 판정
#   X 가 P2 에서 무너지고 P3 에서 복구 + S 는 세 구간 모두 정상  => 노출 발산이 근인 (결정적)
#   X 도 P2 에서 정상                                            => 노출 발산 기각
#   S 도 P2 에서 무너짐                                          => v4l2 쓰기 교란, 실험 무효
#
# 주의
#   subdev 번호는 하드 리셋마다 바뀔 수 있어 **런타임에 exp_time_ch0 을 가진 노드를 찾는다**.
#   쓰기 후 반드시 되읽어 로그에 남긴다(적용됐다고 가정하지 않는다).
#
# 사용법: SAMPLES=45 ROUNDS=3 ./probe-exposure-split.sh
#
set -u

CAM=/root/camtest
RESET="$CAM/cam_hard_reset.sh"
CONF=/root/shared_v/edgeconf_pim.json
OUT=/root/fpsmeas
BACKUP="$OUT/edgeconf.orig.json"
BIN="$OUT/capapp"
SAMPLES=${SAMPLES:-75}
ROUNDS=${ROUNDS:-3}
IVL_MS=${IVL_MS:-1000}
EXPCTL=${EXPCTL:-2000}
EXPSPLIT=${EXPSPLIT:-11000}   # > frame_period(8333) -> over_period=1
EXPUNDER=${EXPUNDER:-4000}    # < frame_period       -> over_period=0
P1_S=${P1_S:-30}          # 개입 시각(초) — 120fps 는 기동에 15s+ 걸린다(리허설 실측)
P2_S=${P2_S:-50}          # 복귀 시각(초)
BPSCTL=${BPSCTL:-4096}
NPROC=$(nproc)

STAMP=$(date +%Y%m%d_%H%M%S)
LOG="$OUT/expsplit_$STAMP.log"
CSV="$OUT/expsplit_$STAMP.csv"
SUM="$OUT/expsplit_${STAMP}_summary.csv"
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

# i2c2(ch0/ch1) 컨트롤을 가진 subdev 를 런타임에 찾는다 — 번호는 리셋마다 바뀔 수 있다.
find_subdev() {
	for n in 0 1 2 3 4 5; do
		d="/dev/v4l-subdev$n"
		[ -e "$d" ] || continue
		if v4l2-ctl -d "$d" -l 2>/dev/null | grep -q 'exp_time_ch0'; then
			echo "$d"; return 0
		fi
	done
	return 1
}

declare -A IRQP
declare -A HINFP
declare -A ADDR

sample_loop() { # $1=라벨 $2=trial $3=DEVSPEC $4=T0(ms) $5=subdev $6=P2값 $7=컨트롤명 $8=P3 복귀값
	SL_LB=$1; SL_TR=$2; SL_DEVS=$3; SL_T0=$4; SL_SD=$5; SL_VAL=$6; SL_CTL=$7; SL_BACK=$8
	MARK1=0; MARK2=0
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

		PH=1; [ "$TS" -ge $((P1_S*1000)) ] && PH=2; [ "$TS" -ge $((P2_S*1000)) ] && PH=3
		echo "$SL_LB,$SL_TR,$TS,$PH,$D_CSI0,,$D_ISI0,,$D_CSI1,$D_ISI1${CSVX}" >>"$CSV"

		# 개입: 쓰기 후 반드시 되읽어 남긴다 (적용됐다고 가정하지 않는다).
		if [ "$MARK1" -eq 0 ] && [ "$TS" -ge $((P1_S*1000)) ]; then
			v4l2-ctl -d "$SL_SD" -c "$SL_CTL"="$SL_VAL" >>"$LOG" 2>&1
			RB=$(v4l2-ctl -d "$SL_SD" -C "$SL_CTL" 2>/dev/null | awk '{print $NF}')
			log "  [P2 진입 ${TS}ms] $SL_CTL <- $SL_VAL (되읽기=${RB:-읽기실패})"
			MARK1=1
		fi
		if [ "$MARK2" -eq 0 ] && [ "$TS" -ge $((P2_S*1000)) ]; then
			v4l2-ctl -d "$SL_SD" -c "$SL_CTL"="$SL_BACK" >>"$LOG" 2>&1
			RB=$(v4l2-ctl -d "$SL_SD" -C "$SL_CTL" 2>/dev/null | awk '{print $NF}')
			log "  [P3 진입 ${TS}ms] $SL_CTL <- $SL_BACK 복귀 (되읽기=${RB:-읽기실패})"
			MARK2=1
		fi

		NEXT=$(( SL_T0 + i * IVL_MS ))
		SLP=$(( NEXT - $(now_ms) ))
		[ "$SLP" -gt 0 ] && sleep "$(awk -v d="$SLP" 'BEGIN{printf "%.3f", d/1000.0}')"
	done
}

# 회차 요약: 구간(P1/P2/P3)별 평균 fps 와 무증분 샘플 수
summarize_run() { # $1=라벨 $2=trial $3=csi열 $4=isi열
	awk -F, -v K="$1" -v T="$2" -v CC="$3" -v IC="$4" -v SUMF="$SUM" -v IVL="$IVL_MS" '
		$1==K && $2==T {
			ph=$4+0; n[ph]++; c=$CC+0; i=$IC+0
			cs[ph]+=c; is[ph]+=i
			if (c==0) z[ph]++
			if (c>0) live[ph]++
		}
		END {
			line=K","T
			for (ph=1; ph<=3; ph++) {
				if (n[ph]+0 == 0) { line=line",,"; continue }
				f = cs[ph]/n[ph]/2.0*1000.0/IVL
				printf "  P%d: 표본 %d / 평균 %.1f fps / CSI 증분 0 인 표본 %d / ISI 합 %d\n",
				       ph, n[ph], f, z[ph]+0, is[ph]+0
				line = sprintf("%s,%.1f,%d", line, f, z[ph]+0)
			}
			print line >> SUMF
		}' "$CSV" | tee -a "$LOG"
}

probe() { # $1=라벨 $2=trial $3=P2값 $4=컨트롤명(기본 exp_time_ch1) $5=P3 복귀값(기본 $EXPCTL)
	LB=$1; TR=$2; VAL=$3; CTL=${4:-exp_time_ch1}; BACK=${5:-$EXPCTL}
	C0=true; C1=true; C2=false; C3=false          # 항상 ch0+ch1 듀얼와이드
	DEVS="ch0=2:0x11,0x3c ch1=2:0x12,0x3c"; CC=5; IC=7
	log "==============================================================="
	log "### $LB  시도 $TR  (P2 에서 $CTL <- $VAL, 120fps, 창 ${SAMPLES}s)"
	log "==============================================================="
	kill_cap
	assert_daemon_off

	# 전 항목을 4채널 동일값으로 누른다. ae_on 도 전부 false — 시드 스킵이 양쪽 다 없다.
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
	    ' "$BACKUP" >"$OUT/.es.json" || return 1
	# cp 도중 죽어도 복원되도록 쓰기 "전"에 세운다
	CONF_DIRTY=1
	cp "$OUT/.es.json" "$CONF"

	# enable 을 뺀 나머지가 4채널 동일해야 한다 (ae_on 포함 — 이번엔 독립변수가 아니다).
	UNIQ=$(jq -r '[.VHL_CAM.i2c2.ch0,.VHL_CAM.i2c2.ch1,.VHL_CAM.i2c1.ch2,.VHL_CAM.i2c1.ch3]
	              | map(del(.enable)) | unique | length' "$CONF")
	if [ "$UNIQ" != "1" ]; then
		log "  !!! 통제 검증 실패 (unique=$UNIQ) — 이 회차 무효 !!!"
	fi
	log "  config: unique(enable 제외)=$UNIQ exp=$(jq -r '[.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]|@csv' "$CONF")"
	log "  ae_on 4채널: $(jq -c '[.VHL_CAM.i2c2.ch0.ae_on,.VHL_CAM.i2c2.ch1.ae_on,.VHL_CAM.i2c1.ch2.ae_on,.VHL_CAM.i2c1.ch3.ae_on]' "$CONF")"

	"$RESET" -q >>"$LOG" 2>&1
	sleep 3

	AL="$OUT/expsplit_app_${LB}_${TR}.log"
	while read -r k v; do IRQP[$k]=$v; done < <(irq_all)
	setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$AL" 2>&1 &
	T0=$(now_ms)

	# 앱이 열린 뒤에 subdev 를 찾는다 — 리셋으로 번호가 바뀌었을 수 있다.
	sleep 5
	SD=$(find_subdev) || { log "  !!! exp_time_ch0 을 가진 subdev 를 못 찾음 — 회차 무효 !!!"; SD=/dev/null; }
	log "  subdev: $SD"

	sample_loop "$LB" "$TR" "$DEVS" "$T0" "$SD" "$VAL" "$CTL" "$BACK"

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

log "=== 노출 발산 판별 $(date -Is) ==="
log "창 ${SAMPLES}s x ${ROUNDS}라운드, 4갈래 교차(2x2: 대칭/비대칭 x over_period 0/1). ch0+ch1 듀얼와이드 120fps, 4채널 ae_on=false 대칭."
log "구간: P1 0~${P1_S}s / P2 ${P1_S}~${P2_S}s (노출 개입) / P3 ${P2_S}s~ (복귀). 통제 exp=$EXPCTL split=$EXPSPLIT under=$EXPUNDER bps=$BPSCTL."
log ""
echo "case,trial,t_ms,phase,csi0_d,,isi0_d,,csi1_d,isi1_d,a_dhinf,a_sfps,b_dhinf,b_sfps" >"$CSV"
echo "case,trial,p1_fps,p1_zero,p2_fps,p2_zero,p3_fps,p3_zero" >"$SUM"

for r in $(seq 1 "$ROUNDS"); do
	log "########## 라운드 $r / $ROUNDS ##########"
	# 2x2:  대칭/비대칭  x  over_period 0/1  (frame_period_us=8333 @120fps)
	probe "S_sham"    "$r" "$EXPCTL"                 # 대칭, over=0  기준선
	probe "X_split"   "$r" "$EXPSPLIT"               # 비대칭, 한쪽 over=1
	probe "Y_under"   "$r" "$EXPUNDER"               # 비대칭, 양쪽 over=0  <- 노출 "차이" 자체를 본다
	probe "Z_botover" "$r" "$EXPSPLIT" exp_time      # 대칭, 양쪽 over=1    <- over_period 자체를 본다
	# 노출은 건드리지 않고 ch1 만 AE auto 로. AE 를 "켤" 때는 :3251 이 노출을 쓰지 않으므로
	# 명시적 노출 쓰기 없이 **AE 수렴값만** 갈린다. 0x3c 통일로 못 막는 경로인지 본다.
	probe "W_aeon"    "$r" 1 ae_on_ch1 0             # 비대칭 AE, 노출 쓰기 없음
done

log "=== 종료 $(date -Is) ==="
