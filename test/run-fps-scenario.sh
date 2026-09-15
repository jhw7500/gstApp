#!/usr/bin/env bash
#
# run-fps-scenario.sh - 채널 조합별 실제 전달 fps 측정 (재사용 시나리오)
#
# 타겟 보드에서 실행한다. 호스트에서 scp 로 올린 뒤 돌린다:
#   scp test/run-fps-scenario.sh root@<board>:/root/fpsmeas/
#   ssh root@<board> '/root/fpsmeas/run-fps-scenario.sh --res 1280x720 --fps 60'
#
# ============================================================================
# 이 스크립트가 구조적으로 막는 것 (전부 2026-09-07 실측으로 확인된 실패)
# ============================================================================
#
# 1. raw v4l2 로 재면 20~25% 낮게 나온다.
#    `media-ctl` + `v4l2-ctl` 로 직접 캡처하면 max9296 `prepare` sysfs 를 거치지
#    않아 디시리얼라이저/AP1302 모드 테이블이 완전 구성되지 않는다. 640x360@120
#    ch0+ch1 이 raw 95.09 vs gstApp 117.5 로 갈렸다. => 측정은 항상 앱으로 한다.
#    이 스크립트는 앱을 띄우고, prepare 상태가 CONSUMED/READY 인지 확인한다.
#
# 2. 고정 워밍업으로 "정상상태"를 자르면 안 된다.
#    스트림이 실제로 흐르기까지 13~20초 걸린다(펌웨어 로드). 고정 8초 워밍업으로
#    쟀더니 모든 값이 (실제 x 스트리밍시간/측정창) 로 축소돼 하드웨어 병목처럼
#    보였다. => ISI 인터럽트 증가로 시작을 감지한 뒤에만 측정창을 연다.
#
# 3. 모드를 바꾸려면 hard reset 이 필요하다.
#    max9296 은 한 hardware epoch 안에서 지문(해상도/fps/enable) 변경을
#    -ESTALE(116) 로 거부한다. => 케이스마다 cam_hard_reset.sh 를 선행한다.
#
# 4. 시험 하네스 명령줄에 앱 이름 리터럴을 넣으면 killcam 이 죽인다.
#    => 바이너리를 다른 이름으로 복사해 실행하고, 정리는 -x(comm 정확일치)로 한다.
#
# 5. 지원되지 않는 fps 는 "느리게 도는" 게 아니라 "무시된다".
#    드라이버가 열거하지 않는 fps 를 요청하면 조용히 기존 fps 로 돈다. 720p@60 을
#    요청했을 때 29.85fps 가 나온 것은 절반으로 떨어진 게 아니라 60 이 거부된 것.
#    => 측정 전에 frame interval 열거로 지원 여부를 먼저 확인한다(--check-only).
#
# 6. 설정을 건드리므로 반드시 복원한다.
#    => md5 백업 + trap 으로 어떤 종료 경로에서도 복원하고, md5 로 검증한다.
#
# 판정 오라클 3종을 모두 기록해 서로 교차검증한다:
#   - CSI2 인터럽트 / 2   (프레임당 Frame Start + Frame End, 해상도 무관)
#   - ISI 인터럽트         (보통 프레임당 1회. 고부하에서 흔들릴 수 있어 참고값)
#   - gstApp enc-stat      (앱이 인코더에 넣은 프레임 수 = 최종 전달률)
#
# 하드웨어 배치:
#   2-0048 = ch0/ch1 -> csi 32e50000, isi 32e02000, /dev/video4, subdev2
#   1-0048 = ch2/ch3 -> csi 32e40000, isi 32e00000, /dev/video3, subdev3
#   enable 비트: 1=left(ch0/ch2), 2=right(ch1/ch3), 3=dual-wide(폭 2배)
#
set -u

# ---------------------------------------------------------------- 기본값/옵션
RES=640x360
FPS=60
COMBOS="0+1 0+2"
REPEAT=3
DUR=20
SETTLE=5
STARTWAIT=60
CHECK_ONLY=0
# 노출은 양 버스에 **반드시 같은 값을 강제**한다. 운영 config 는 버스별로 다르게 준다
# (실측 2026-09-07: i2c2=2000us, i2c1=50000us). 그대로 두면 fps 가 버스별로 갈리고
# 그 차이를 "버스/하드웨어 특성"으로 오독하게 된다 - 실제로 그렇게 오독한 적이 있다.
# 노출 기본값은 **요청 fps 에서 유도한다** — 프레임주기의 0.6배.
#   60fps  -> 주기 16,667us -> 10,000us   (실측 최적점 11,000 부근, 평탄구간 8,000~14,000)
#   120fps -> 주기  8,333us ->  5,000us
#   30fps  -> 주기 33,333us -> 20,000us
# 고정값을 쓰면 fps 를 바꿨을 때 노출이 프레임주기를 넘어 U 곡선 오른쪽으로 넘어간다
# (120fps 에 10,000us 를 쓰면 노출 > 주기 8,333us). -x/--exp 로 명시하면 그 값이 우선한다.
EXP=""
EXP_AUTO=1
# --prod-cam: 카메라 설정(ae_on, ae_gain, led_flash, exp_time, flip, awb, dz)을 **운영값 그대로**
# 두고 해상도/fps/채널만 바꾼다. "운영에서 실제로 몇 fps 가 나오는가" 를 재는 모드다.
# 기본(통제 모드)은 LED off + ae_on=false + 양 버스 exp 통일로 변수를 묶는다.
# 운영은 채널별로 섞여 있다(실측: ae_on ch0=false ch1=true ch2=true ch3=false,
# led_flash ch0/ch2=true, exp_time i2c2=2000 i2c1=50000) - 두 모드의 값은 서로 다르다.
PROD_CAM=0
# 통제 모드의 카메라 조건. 측정 채널은 ae_on=true(드라이버 기본값)로 두고 LED 는 끈다.
# LED 를 켜고 시험할 때는 flash_delay 를 0 으로 고정한다(스크립트가 항상 0 으로 쓴다).
AE_ON=true
LED_ON=false

CAM=${CAM_DIR:-/root/camtest}
RESET="$CAM/cam_hard_reset.sh"
# gstApp 이 읽는 병합 문서. 생산자 입력(/root/shared_v/edgeconf_pim.json)을 고쳐도
# 이 스크립트는 cam-operate 를 멈춘 채 돌아 반영되지 않는다 (이슈 #113).
# 두 층의 관계와 근거: docs/FPS_MEASUREMENT_SCENARIO.md §8.1
CONF=${RUNTIME_CONF:-/run/pim-camera/config/pim_runtime.json}
# 서비스가 멈추면 systemd 가 RuntimeDirectory 를 지우므로 쓰기 전마다 되살린다.
# 같은 디렉터리 임시 파일 + mv 로 발행한다(생산자 write_json_atomic 과 같은 원자성).
# install -d 가 아니라 mkdir -p -m 인 이유, 경로 형태와 파일 종류를 먼저 보는 이유는
# 같은 문서 §8.2. 이 정의는 13 벌이 바이트 동일해야 한다(게이트가 검사한다).
# shellcheck disable=SC2174  # 기본 CONF 기준이다 — RUNTIME_CONF 로 더 깊은 경로를 주면 중간 요소는 -m 을 못 받고 umask 를 따른다(docs §8.2 실측)
put_conf() { case $CONF in /*/*/*) ;; *) echo "!! CONF 가 /a/b/c 형태가 아니다: $CONF" >&2; return 1;; esac; if [ -L "$CONF" ] || { [ -e "$CONF" ] && [ ! -f "$CONF" ]; }; then echo "!! $CONF 가 정규 파일이 아니다 - 쓰지 않는다" >&2; return 1; fi; if mkdir -p -m 0750 "${CONF%/*/*}" "${CONF%/*}" && cp -f "$1" "$CONF.tmp.$$" && chmod 0640 "$CONF.tmp.$$" && mv -f "$CONF.tmp.$$" "$CONF"; then return 0; fi; rm -f "$CONF.tmp.$$"; echo "!! config 쓰기 실패: $1 -> $CONF" >&2; return 1; }
# 예전 이름 EDGECONF 는 edge 문서를 가리켰다. 그냥 무시하면 그 값이 조용히 버려지고
# 기본값으로 측정이 돌아 — 이 이슈가 만든 것과 같은 종류의 거짓 성공이 된다. 멈춘다.
if [ -n "${EDGECONF:-}" ]; then
	echo "중단: EDGECONF 는 RUNTIME_CONF 로 바뀌었습니다 (이슈 #113)."
	echo "  EDGECONF 는 생산자의 입력(/root/shared_v/edgeconf_pim.json)을 가리켰지만,"
	echo "  gstApp 은 병합 문서 $CONF 만 읽습니다."
	echo "  병합 문서를 직접 지정하려면 RUNTIME_CONF 를 쓰세요."
	exit 2
fi
SRC_BIN=${SRC_BIN:-/usr/local/bin/gstApp}
OUT=${OUT:-/root/fpsmeas}
# killcam 은 명령줄에 앱 이름 리터럴이 있는 프로세스를 죽인다. 다른 이름을 쓴다.
TEST_BIN_NAME=${TEST_BIN_NAME:-capapp}

usage() {
	sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//'
	cat <<'USAGE'

사용법:
  run-fps-scenario.sh [옵션]
    -r, --res WxH       해상도. 기본 640x360
    -f, --fps N         요청 fps. 기본 60
    -k, --combos "..."  채널 조합, 공백 구분. 기본 "0+1 0+2"
                        예: "0+1" "0+2" "0+1 0+2 0+1+2+3" "0"
    -n, --repeat N      조합당 반복. 기본 3
    -d, --duration N    측정 구간(초). 기본 20
    -x, --exp N         노출(us). **양 버스에 같은 값을 강제**한다.
                        생략하면 **요청 fps 에서 유도**한다(프레임주기의 0.6배):
                          60fps->10000, 120fps->5000, 30fps->20000
                        운영 config 는 버스별로 다르게 준다(i2c2=2000, i2c1=50000).
                        그대로 두면 fps 가 버스별로 갈린다 - 실측으로 확인됨.
                        노출이 프레임주기 이상이면 경고한다.
        --settle N      스트림 감지 후 안정화(초). 기본 5
        --ae true|false 측정 채널 4개의 ae_on. 기본 true (드라이버 기본값)
        --led true|false 측정 채널 4개의 led_flash.enable. 기본 false.
                        켜든 끄든 flash_delay 는 항상 0 으로 쓴다.
        --prod-cam      카메라 설정(ae_on·ae_gain·led_flash·exp_time·flip·awb)을 **운영값 그대로**
                        두고 해상도/fps/채널만 바꾼다. "운영에서 실제로 몇 fps 인가" 측정용.
                        기본은 통제 모드(LED off + ae_on=false + 양버스 노출통일).
        --check-only    지원 여부(frame interval 열거)만 확인하고 종료
    -h, --help

  환경변수: SRC_BIN(기본 /usr/local/bin/gstApp), RUNTIME_CONF, CAM_DIR, OUT,
            TEST_BIN_NAME(기본 capapp)

예:
  # 드라이버가 720p@60 을 지원하는지 먼저 확인 (앱 기동 없음, 빠름)
  run-fps-scenario.sh --res 1280x720 --fps 60 --check-only
  # 지원하면 본 측정
  run-fps-scenario.sh --res 1280x720 --fps 60 -k "0+1 0+2" -n 3
  # 360p 고fps
  run-fps-scenario.sh --res 640x360 --fps 120 -k "0+1 0+2" -n 3
USAGE
}

while [ $# -gt 0 ]; do
	case "$1" in
	-r | --res)
		RES="$2"
		shift
		;;
	-f | --fps)
		FPS="$2"
		shift
		;;
	-k | --combos)
		COMBOS="$2"
		shift
		;;
	-n | --repeat)
		REPEAT="$2"
		shift
		;;
	-d | --duration)
		DUR="$2"
		shift
		;;
	--settle)
		SETTLE="$2"
		shift
		;;
	-x | --exp)
		EXP="$2"
		EXP_AUTO=0
		shift
		;;
	--ae)
		AE_ON="$2"
		shift
		;;
	--led)
		LED_ON="$2"
		shift
		;;
	--prod-cam) PROD_CAM=1 ;;
	--check-only) CHECK_ONLY=1 ;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		echo "알 수 없는 옵션: $1" >&2
		exit 1
		;;
	esac
	shift
done

W=${RES%x*}
H=${RES#*x}
case "$W" in '' | *[!0-9]*) echo "해상도 형식 오류: $RES (WxH)" >&2; exit 1 ;; esac
case "$H" in '' | *[!0-9]*) echo "해상도 형식 오류: $RES (WxH)" >&2; exit 1 ;; esac
case "$FPS" in '' | *[!0-9]* | 0) echo "fps 오류: $FPS" >&2; exit 1 ;; esac

# 노출 자동 유도: 프레임주기(1e6/fps)의 0.6배. --exp 를 주면 그 값이 우선한다.
if [ "$EXP_AUTO" -eq 1 ]; then
	EXP=$((600000 / FPS))
fi
PERIOD_US=$((1000000 / FPS))
if [ "$EXP" -ge "$PERIOD_US" ]; then
	echo "경고: 노출 ${EXP}us 가 프레임주기 ${PERIOD_US}us(=${FPS}fps) 이상이다." >&2
	echo "      이 조건은 센서가 프레임을 늘려 fps 가 요청치에 못 미친다(U 곡선 오른쪽)." >&2
fi

# $CONF 는 cam-operate 가 도는 동안만 있다 (같은 문서 §8.2). 기존 존재 검사에
# 그 안내를 얹는다 — 검사를 하나 더 두면 같은 조건을 두 번 보게 된다.
for t in "$RESET" "$SRC_BIN" "$CONF"; do
	[ -e "$t" ] || {
		echo "필요한 파일 없음: $t" >&2
		[ "$t" = "$CONF" ] && echo "      병합 문서는 cam-operate 가 도는 동안만 있다 — 먼저 기동할 것" >&2
		exit 2
	}
done
for t in jq v4l2-ctl media-ctl; do
	command -v "$t" >/dev/null || {
		echo "필요한 명령 없음: $t" >&2
		exit 2
	}
done

# 자기 명령줄에 앱 이름 리터럴이 있으면 killcam 의 표적이 된다. 미리 거부한다.
if tr '\0' ' ' </proc/$$/cmdline | grep -q "$(basename "$SRC_BIN")"; then
	echo "명령줄에 '$(basename "$SRC_BIN")' 리터럴이 있다 - killcam 이 이 스크립트를 죽인다." >&2
	echo "  SRC_BIN 을 환경변수로 넘겨라: SRC_BIN=... $0 ..." >&2
	exit 3
fi

mkdir -p "$OUT"
STAMP=$(date +%Y%m%d_%H%M%S)
TAG="${W}x${H}@${FPS}"
LOG="$OUT/fps_${TAG}_$STAMP.log"
CSV="$OUT/fps_${TAG}_$STAMP.csv"
BACKUP="$OUT/pim_runtime.orig.json"
BIN="$OUT/$TEST_BIN_NAME"

log() { echo "$*" | tee -a "$LOG"; }

# --------------------------------------------------------------- 설정 백업
if [ ! -e "$BACKUP" ]; then
	cp "$CONF" "$BACKUP" || exit 2
	md5sum "$BACKUP" | awk '{print $1}' >"$BACKUP.md5"
fi
ORIG_MD5=$(cat "$BACKUP.md5")
LIVE_MD5=$(md5sum "$CONF" | awk '{print $1}')
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
		echo "  앞선 회차가 남긴 시험 config 일 수 있습니다. 되돌아가도 좋다면"
		echo "  ALLOW_CONF_DRIFT=1 로 다시 실행하세요(측정 뒤 백업 내용으로 복원됩니다)."
		echo "  백업을 live 로 덮지 마세요 - 이후 회차가 그것을 '원본' 으로 발행합니다."
		exit 2
	fi
fi
# trap 은 첫 config 쓰기보다 먼저 걸리므로, 덮어쓴 회차에서만 복원한다.
CONF_DIRTY=0
# 이 실행이 시작된 시각. 녹화 삭제를 이 시점 이후 파일로만 한정한다 — 고정 10분 창은
# /dev/shm 과 운영 tmp_path/sd_tmp_path 를 훑어 직전 운영 녹화까지 지웠다.
RUN_T0=$(date +%s)

WAS_ACTIVE=0
systemctl is-active --quiet cam-operate.service && WAS_ACTIVE=1
[ "${RESTORE_CAM_OPERATE:-0}" = "1" ] && WAS_ACTIVE=1

kill_app() {
	pkill -x "$TEST_BIN_NAME" 2>/dev/null
	for _ in $(seq 1 20); do
		pgrep -x "$TEST_BIN_NAME" >/dev/null 2>&1 || return 0
		sleep 1
	done
	pkill -9 -x "$TEST_BIN_NAME" 2>/dev/null
	sleep 2
}

restore() {
	rc=$?
	log ""
	log "### 복구"
	kill_app
	if [ "$CONF_DIRTY" -eq 1 ]; then
		put_conf "$BACKUP"
		NOW=$(md5sum "$CONF" | awk '{print $1}')
		if [ "$NOW" = "$ORIG_MD5" ]; then
			log "설정 복원 md5 일치 ($NOW)"
		else
			log "!!! 설정 복원 md5 불일치: $NOW != $ORIG_MD5 - 수동 확인 필요 !!!"
		fi
	else
		log "설정을 덮어쓴 적이 없어 복원을 생략합니다 (live 유지)"
	fi
	"$RESET" -q >>"$LOG" 2>&1
	sleep 2
	if [ "$WAS_ACTIVE" -eq 1 ]; then
		systemctl start cam-operate.service >>"$LOG" 2>&1
		sleep 12
	else
		log "!! cam-operate 를 정지 상태로 둡니다 (시작 시에도 정지 상태였음)."
		log "!! 보드가 녹화하지 않습니다. 되살리려면 RESTORE_CAM_OPERATE=1 또는 수동 기동."
	fi
	log "cam-operate: $(systemctl is-active cam-operate.service)"
	log "### 로그: $LOG"
	[ "$CHECK_ONLY" -eq 1 ] || log "### CSV: $CSV"
	exit $rc
}
trap restore EXIT INT TERM

# ------------------------------------------------------------------ 유틸
NCPU=$(nproc 2>/dev/null || echo 4)
irq_of() {
	awk -v d="$1" -v n="$NCPU" '$NF==d { s=0; for (i=2;i<=n+1;i++) s+=$i; print s+0; exit }' /proc/interrupts
}
csi_irq() { case "$1" in 0) echo "32e50000.csi" ;; 1) echo "32e40000.csi" ;; esac; }
isi_irq() { case "$1" in 0) echo "32e02000.isi" ;; 1) echo "32e00000.isi" ;; esac; }
i2c_of() { case "$1" in 0) echo "2-0048" ;; 1) echo "1-0048" ;; esac; }
subdev_of() { case "$1" in 0) echo 2 ;; 1) echo 3 ;; esac; }

# 조합 문자열 -> 채널별 true/false. 예: "0+2" -> "true false true false"
combo_flags() {
	_c=$1
	_o=""
	for ch in 0 1 2 3; do
		case "+$_c+" in
		*"+$ch+"*) _o="$_o true" ;;
		*) _o="$_o false" ;;
		esac
	done
	echo "${_o# }"
}

hard_reset() {
	for _try in 1 2 3; do
		pkill -x v4l2-ctl 2>/dev/null
		sleep 1
		"$RESET" -q >>"$LOG" 2>&1 && {
			sleep 3
			return 0
		}
		sleep 3
	done
	log "  !! hard reset 3회 실패"
	return 1
}

# CSI 별 enable 마스크(2비트)와 그 CSI 가 실어 나르는 프레임 폭
csi_enable() { # $1=csi(0|1) $2..$5 = ch0..ch3 flags
	_csi=$1
	shift
	_a=$1
	_b=$2
	_c=$3
	_d=$4
	if [ "$_csi" = "0" ]; then _x=$_a _y=$_b; else _x=$_c _y=$_d; fi
	_m=0
	[ "$_x" = "true" ] && _m=$((_m | 1))
	[ "$_y" = "true" ] && _m=$((_m | 2))
	echo "$_m"
}

# ------------------------------------------------- 사전 점검: 모드 지원 여부
# frame interval 열거는 드라이버가 스스로 답하는 값이라 prepare 와 무관하다.
# 여기서 요청 fps 가 안 나오면 측정은 의미가 없다(요청이 조용히 무시된다).
check_support() { # $1=combo
	_flags=$(combo_flags "$1")
	set -- $_flags
	_ok=1
	hard_reset
	for csi in 0 1; do
		_m=$(csi_enable "$csi" "$@")
		echo "$_m" >"/sys/bus/i2c/devices/$(i2c_of "$csi")/enable" 2>/dev/null
	done
	sleep 2
	for csi in 0 1; do
		_m=$(csi_enable "$csi" "$@")
		[ "$_m" = "0" ] && continue
		_w=$W
		[ "$_m" = "3" ] && _w=$((W * 2))
		_sd=$(subdev_of "$csi")
		_max=$(v4l2-ctl -d "/dev/v4l-subdev$_sd" \
			--list-subdev-frameintervals "pad=0,code=0x2006,width=$_w,height=$H" 2>/dev/null |
			grep -oE '\([0-9.]+ fps\)' | tr -d '(fps)' | sort -n | tail -1)
		if [ -z "$_max" ]; then
			log "  [csi$csi] ${_w}x${H}: frame interval 열거 실패 (모드 미지원 가능)"
			_ok=0
			continue
		fi
		_has=$(v4l2-ctl -d "/dev/v4l-subdev$_sd" \
			--list-subdev-frameintervals "pad=0,code=0x2006,width=$_w,height=$H" 2>/dev/null |
			grep -cE "\($FPS\.000 fps\)")
		if [ "$_has" -ge 1 ]; then
			log "  [csi$csi] ${_w}x${H}: ${FPS}fps 지원됨 (열거 최대 ${_max}fps)"
		else
			log "  [csi$csi] ${_w}x${H}: **${FPS}fps 미지원** — 열거 최대 ${_max}fps"
			log "            이 상태로 요청하면 조용히 무시되고 다른 fps 로 돈다."
			_ok=0
		fi
	done
	return $((1 - _ok))
}

# ------------------------------------------------------------------- 시작
log "=== fps 시나리오 $(date -Is) ==="
log "해상도=${W}x${H} 요청fps=$FPS 조합=[$COMBOS] 반복=$REPEAT 측정=${DUR}s"
log "드라이버 max9296 version: $(cat /sys/module/max9296/version 2>/dev/null || echo '(모듈 아님/미확인)')"
log "앱 바이너리: $SRC_BIN -> $BIN"
log "설정: $CONF (백업 $BACKUP md5 $ORIG_MD5)"

if systemctl is-active --quiet cam-operate.service; then
	systemctl stop cam-operate.service >>"$LOG" 2>&1
	sleep 5
	log "cam-operate 정지: $(systemctl is-active cam-operate.service)"
fi
pkill -x killcam 2>/dev/null
sleep 2

# fps 예산 (max9296Controls.cpp: 360p/720p=240, 1080p=180, 그 외 미지원)
budget=0
case "${W}x${H}" in
640x360 | 1280x720) budget=240 ;;
1920x1080) budget=180 ;;
*)
	log "!! ${W}x${H} 는 앱이 지원하지 않는 해상도다 (640x360/1280x720/1920x1080 만)"
	exit 4
	;;
esac

log ""
log "### 사전 점검 - 모드 지원 여부"
SUPPORTED=""
for combo in $COMBOS; do
	nch=$(combo_flags "$combo" | tr ' ' '\n' | grep -c true)
	total=$((nch * FPS))
	log "[$combo] 채널 ${nch}개, total_fps=$total / 예산 $budget"
	if [ "$total" -gt "$budget" ]; then
		log "  !! fps 예산 초과 - 앱이 기동을 거부한다. 건너뜀"
		continue
	fi
	if check_support "$combo"; then
		SUPPORTED="$SUPPORTED $combo"
	else
		log "  !! 미지원 조합 - 측정에서 제외"
	fi
done
SUPPORTED=${SUPPORTED# }

if [ "$CHECK_ONLY" -eq 1 ]; then
	log ""
	log "=== 점검 종료: 측정 가능한 조합 = [${SUPPORTED:-없음}] ==="
	exit 0
fi
[ -n "$SUPPORTED" ] || {
	log "측정 가능한 조합이 없다."
	exit 5
}

cp "$SRC_BIN" "$BIN" || { log "중단: 앱 스테이징 복사 실패"; exit 2; }
chmod +x "$BIN" || { log "중단: 앱 스테이징 chmod 실패"; exit 2; }
echo "case,combo,attempt,req_fps,exp_us,channel,csi_fps,isi_fps,encstat_fps,start_lat_s,elapsed_s" >"$CSV"

# ------------------------------------------------------------------ 본 측정
for combo in $SUPPORTED; do
	FLAGS=$(combo_flags "$combo")
	set -- $FLAGS
	C0=$1 C1=$2 C2=$3 C3=$4
	for r in $(seq 1 "$REPEAT"); do
		log ""
		log "==================================================================="
		log "### ${W}x${H}@${FPS} ch[$combo] 시도 $r/$REPEAT"
		log "==================================================================="
		kill_app

		# --prod-cam 이면 카메라 설정을 운영값 그대로 두고 해상도/fps/채널만 바꾼다.
		jq --argjson f "$FPS" --argjson w "$W" --argjson h "$H" --argjson e "$EXP" \
			--argjson prod "$PROD_CAM" --argjson ae "$AE_ON" --argjson led "$LED_ON" \
			--argjson c0 "$C0" --argjson c1 "$C1" --argjson c2 "$C2" --argjson c3 "$C3" '
        .VHL_CAM.cam_width  = $w
      | .VHL_CAM.cam_height = $h
      | .VHL_CAM.fps        = $f
      | .VHL_CAM.debug_level = 5
      | .VHL_CAM.queue_tune.enc_stat_sec = 5
      | .VHL_CAM.i2c2.ch0.enable = $c0 | .VHL_CAM.i2c2.ch1.enable = $c1
      | .VHL_CAM.i2c1.ch2.enable = $c2 | .VHL_CAM.i2c1.ch3.enable = $c3
      | (if $prod == 1 then . else
            .VHL_CAM.i2c2.exp_time = $e
          | .VHL_CAM.i2c1.exp_time = $e
          | .VHL_CAM.i2c2.ch0.led_flash.enable = $led
          | .VHL_CAM.i2c2.ch1.led_flash.enable = $led
          | .VHL_CAM.i2c1.ch2.led_flash.enable = $led
          | .VHL_CAM.i2c1.ch3.led_flash.enable = $led
          | .VHL_CAM.i2c2.ch0.led_flash.flash_delay = 0
          | .VHL_CAM.i2c2.ch1.led_flash.flash_delay = 0
          | .VHL_CAM.i2c1.ch2.led_flash.flash_delay = 0
          | .VHL_CAM.i2c1.ch3.led_flash.flash_delay = 0
          | .VHL_CAM.i2c2.ch0.ae_on = $ae | .VHL_CAM.i2c2.ch1.ae_on = $ae
          | .VHL_CAM.i2c1.ch2.ae_on = $ae | .VHL_CAM.i2c1.ch3.ae_on = $ae
        end)
      ' "$BACKUP" >"$OUT/.test.json" || {
			log "  jq 실패 - 회차 폐기"
			continue
		}

		# 해상도/fps/채널은 두 모드 공통으로 검증한다.
		VERIFY=$(jq -r '[(.VHL_CAM.cam_width|tostring),(.VHL_CAM.cam_height|tostring),
                     (.VHL_CAM.fps|tostring),
                     (.VHL_CAM.i2c2.ch0.enable|tostring),(.VHL_CAM.i2c2.ch1.enable|tostring),
                     (.VHL_CAM.i2c1.ch2.enable|tostring),(.VHL_CAM.i2c1.ch3.enable|tostring)]
                     |join(" ")' "$OUT/.test.json")
		# 카메라 설정은 모드와 무관하게 **실제 값을 로그에 남긴다**(무엇으로 쟀는지 기록).
		CAMSET=$(jq -r '"exp(i2c2/i2c1)=" + (.VHL_CAM.i2c2.exp_time|tostring) + "/" + (.VHL_CAM.i2c1.exp_time|tostring)
                    + "  ae_on=" + ([.VHL_CAM.i2c2.ch0.ae_on,.VHL_CAM.i2c2.ch1.ae_on,
                                     .VHL_CAM.i2c1.ch2.ae_on,.VHL_CAM.i2c1.ch3.ae_on]|map(tostring)|join(","))
                    + "  led=" + ([.VHL_CAM.i2c2.ch0.led_flash.enable,.VHL_CAM.i2c2.ch1.led_flash.enable,
                                   .VHL_CAM.i2c1.ch2.led_flash.enable,.VHL_CAM.i2c1.ch3.led_flash.enable]|map(tostring)|join(","))
                    + "  ae_gain=" + ([.VHL_CAM.i2c2.ch0.ae_gain,.VHL_CAM.i2c2.ch1.ae_gain,
                                       .VHL_CAM.i2c1.ch2.ae_gain,.VHL_CAM.i2c1.ch3.ae_gain]|map(tostring)|join(","))' \
			"$OUT/.test.json")
		log "  설정: $VERIFY   [$([ "$PROD_CAM" -eq 1 ] && echo '운영 카메라설정' || echo '통제 카메라설정')]"
		log "  카메라: $CAMSET"
		if [ "$VERIFY" != "$W $H $FPS $C0 $C1 $C2 $C3" ]; then
			log "  !! 해상도/fps/채널 검증 실패 - 회차 폐기 (기대: $W $H $FPS $C0 $C1 $C2 $C3)"
			continue
		fi
		if [ "$PROD_CAM" -eq 0 ]; then
			CTRL=$(jq -r '[([.VHL_CAM.i2c2.ch0.ae_on,.VHL_CAM.i2c2.ch1.ae_on,
                        .VHL_CAM.i2c1.ch2.ae_on,.VHL_CAM.i2c1.ch3.ae_on]
                       |map(tostring)|unique|join(",")),
                      ([.VHL_CAM.i2c2.ch0.led_flash.enable,.VHL_CAM.i2c2.ch1.led_flash.enable,
                        .VHL_CAM.i2c1.ch2.led_flash.enable,.VHL_CAM.i2c1.ch3.led_flash.enable]
                       |map(tostring)|unique|join(",")),
                      ([.VHL_CAM.i2c2.ch0.led_flash.flash_delay,.VHL_CAM.i2c2.ch1.led_flash.flash_delay,
                        .VHL_CAM.i2c1.ch2.led_flash.flash_delay,.VHL_CAM.i2c1.ch3.led_flash.flash_delay]
                       |map(tostring)|unique|join(",")),
                      ([.VHL_CAM.i2c2.exp_time,.VHL_CAM.i2c1.exp_time]
                       |map(tostring)|unique|join(","))]|join(" ")' "$OUT/.test.json")
			if [ "$CTRL" != "$AE_ON $LED_ON 0 $EXP" ]; then
				log "  !! 통제 검증 실패 - 회차 폐기: [$CTRL]"
				log "     기대: ae=$AE_ON led=$LED_ON flash_delay=0 exp=$EXP (전부 4채널 균일)"
				continue
			fi
		fi
		# 생산자가 아직 살아 있으면 쓰지 않는다 — 발행 문서를 생산자 검증 없이 덮고
		# publish 와 경합한다. 종료코드로 판정하면 안 된다: 비활성도 조회 실패도 non-zero 라
		# 구분되지 않아, 조회가 깨지면 생산자가 도는 중에도 그대로 쓴다(실측 systemd 249 —
		# inactive rc=3, D-Bus 실패 rc=1 이고 후자는 stdout 이 빈다). 상태 문자열로 보고
		# 모르는 상태·조회 실패에서는 중단한다(이슈 #113 PR 리뷰, Codex P1 ②).
		CAM_STATE=$(systemctl is-active cam-operate.service 2>/dev/null); case $CAM_STATE in inactive|failed) ;; *) echo "!! cam-operate 상태가 [${CAM_STATE:-조회실패}] 다 - 시험 config 를 쓰지 않는다" >&2; exit 2;; esac
		# cp 도중 죽어도 복원되도록 쓰기 "전"에 세운다
		CONF_DIRTY=1
		put_conf "$OUT/.test.json" || exit 2

		hard_reset
		APPLOG="$OUT/app_${TAG}_${combo}_$r.log"
		setsid "$BIN" -d 5 -m 4 -g 5 </dev/null >"$APPLOG" 2>&1 &
		sleep 2

		# --- 스트림 시작 감지 (고정 워밍업 금지) ---
		ACTIVE_CSI=""
		for csi in 0 1; do
			[ "$(csi_enable "$csi" $FLAGS)" != "0" ] && ACTIVE_CSI="$ACTIVE_CSI $csi"
		done
		ACTIVE_CSI=${ACTIVE_CSI# }
		TS=$(date +%s%N)
		OK=0
		for _ in $(seq 1 "$STARTWAIT"); do
			ALL=1
			for csi in $ACTIVE_CSI; do
				A=$(irq_of "$(isi_irq "$csi")")
				sleep 1
				B=$(irq_of "$(isi_irq "$csi")")
				[ $((B - A)) -ge $((FPS / 4)) ] || ALL=0
			done
			[ "$ALL" -eq 1 ] && {
				OK=1
				break
			}
			pgrep -x "$TEST_BIN_NAME" >/dev/null 2>&1 || {
				log "  !! 앱이 죽었다 - 회차 폐기"
				break
			}
		done
		LAT=$(awk -v a="$TS" -v b="$(date +%s%N)" 'BEGIN{printf "%.1f",(b-a)/1e9}')
		if [ "$OK" -ne 1 ]; then
			log "  !! ${STARTWAIT}s 안에 스트림이 시작되지 않았다 (${LAT}s) - 회차 폐기"
			tail -5 "$APPLOG" | sed 's/^/    /' | tee -a "$LOG"
			kill_app
			continue
		fi

		# prepare 가 실제로 소비됐는지 확인 - 이게 raw v4l2 와의 결정적 차이다
		PREP_OK=1
		for csi in $ACTIVE_CSI; do
			P=$(cat "/sys/bus/i2c/devices/$(i2c_of "$csi")/prepare" 2>/dev/null)
			log "  prepare[csi$csi]: $(echo "$P" | tr ' ' '\n' | grep -E '^(state|mode|width|height|fps|enable|match)=' | tr '\n' ' ')"
			case "$P" in *state=CONSUMED* | *state=READY*) ;; *) PREP_OK=0 ;; esac
		done
		[ "$PREP_OK" -eq 1 ] || log "  !! prepare 가 CONSUMED/READY 가 아니다 - 값을 신뢰하지 말 것"

		log "  스트리밍 확인 (${LAT}s), ${SETTLE}s 안정화 후 ${DUR}s 측정"
		sleep "$SETTLE"

		# --- 정상상태 구간 측정 ---
		# 측정창 중에 앱이 재시작하면 인터럽트 델타가 재초기화 구간을 포함해
		# 값이 무의미해진다(ISI=0 같은 값이 나온다). pid+경과시간으로 감지한다.
		APID0=$(pgrep -x "$TEST_BIN_NAME" | head -1)
		AET0=$(ps -o etimes= -p "${APID0:-0}" 2>/dev/null | tr -d ' ')
		T0=$(date +%s%N)
		CA0=$(irq_of "$(csi_irq 0)") IA0=$(irq_of "$(isi_irq 0)")
		CB0=$(irq_of "$(csi_irq 1)") IB0=$(irq_of "$(isi_irq 1)")
		sleep "$DUR"
		T1=$(date +%s%N)
		CA1=$(irq_of "$(csi_irq 0)") IA1=$(irq_of "$(isi_irq 0)")
		CB1=$(irq_of "$(csi_irq 1)") IB1=$(irq_of "$(isi_irq 1)")
		EL=$(awk -v a="$T0" -v b="$T1" 'BEGIN{printf "%.3f",(b-a)/1e9}')

		APID1=$(pgrep -x "$TEST_BIN_NAME" | head -1)
		AET1=$(ps -o etimes= -p "${APID1:-0}" 2>/dev/null | tr -d ' ')
		if [ -z "$APID1" ] || [ "$APID0" != "$APID1" ] ||
			[ "${AET1:-0}" -lt "${AET0:-0}" ]; then
			log "  !! 측정창 중 앱이 재시작/종료됐다 (pid $APID0 -> ${APID1:-없음}) - 회차 폐기"
			kill_app
			sleep 2
			continue
		fi

		rate() { awk -v d="$1" -v k="$2" -v e="$EL" 'BEGIN{printf "%.2f", d/k/e}'; }

		# --- 앱이 실제로 프레임을 받았는지 검증 (CSI2 만으로는 못 가린다) ---
		# CSI2 카운터는 **디시리얼라이저가 SoC 로 보낸** 프레임을 센다. 파이프라인이
		# PAUSED 로 막혀 앱이 한 장도 못 받아도 링크가 살아 있으면 계속 올라간다.
		# 실측 2026-09-07: CSI2 가 111.9fps 를 찍는 동안 앱은 `NO DATA`, enc-stat in=0 이었다.
		# => enc-stat 을 게이트로 쓴다. 이걸 안 하면 죽은 파이프라인을 정상으로 오독한다.
		if grep -q "NO DATA for" "$APPLOG"; then
			log "  !! 앱이 프레임을 받지 못했다 (NO DATA - 파이프라인 PAUSED) - 회차 폐기"
			grep -m2 "NO DATA for" "$APPLOG" | sed 's/^/     /' | tee -a "$LOG"
			kill_app
			sleep 2
			continue
		fi
		ENC_BAD=0
		for ch in 0 1 2 3; do
			case "$ch" in
			0) on=$C0 ;;
			1) on=$C1 ;;
			2) on=$C2 ;;
			*) on=$C3 ;;
			esac
			[ "$on" = "true" ] || continue
			E=$(grep -oE "ch$ch enc-stat\[[0-9]+s\] in=[0-9]+\([0-9.]+fps\)" "$APPLOG" |
				tail -1 | grep -oE '\([0-9.]+fps\)' | tr -d '(fps)')
			if [ -z "$E" ] || awk -v v="$E" 'BEGIN{exit !(v < 1.0)}'; then
				log "  !! ch$ch enc-stat=${E:-없음} — 앱이 프레임을 못 받았다"
				ENC_BAD=1
			fi
		done
		if [ "$ENC_BAD" -eq 1 ]; then
			log "  !! enc-stat 검증 실패 - 회차 폐기 (CSI2 값은 링크만 살아있어도 올라간다)"
			kill_app
			sleep 2
			continue
		fi

		# 채널별로 행을 쓴다. 듀얼와이드(enable=3)는 한 프레임에 두 채널이 실리므로
		# 그 CSI 의 프레임률이 두 채널 각각의 fps 와 같다.
		for ch in 0 1 2 3; do
			case "$ch" in 0 | 1) csi=0 ;; *) csi=1 ;; esac
			case "$ch" in
			0) on=$C0 ;;
			1) on=$C1 ;;
			2) on=$C2 ;;
			*) on=$C3 ;;
			esac
			[ "$on" = "true" ] || continue
			if [ "$csi" = "0" ]; then
				CF=$(rate $((CA1 - CA0)) 2)
				IF=$(rate $((IA1 - IA0)) 1)
			else
				CF=$(rate $((CB1 - CB0)) 2)
				IF=$(rate $((IB1 - IB0)) 1)
			fi
			EF=$(grep -oE "ch$ch enc-stat\[[0-9]+s\] in=[0-9]+\([0-9.]+fps\)" "$APPLOG" |
				tail -1 | grep -oE '\([0-9.]+fps\)' | tr -d '(fps)')
			[ -z "$EF" ] && EF="-"
			log "  >> ch$ch: CSI2=${CF}fps ISI=${IF}fps enc-stat=${EF}fps"
			echo "${W}x${H}@${FPS},$combo,$r,$FPS,$EXP,ch$ch,$CF,$IF,$EF,$LAT,$EL" >>"$CSV"
		done

		kill_app
		for D in /dev/shm "$(jq -r '.VHL_CAM.tmp_path // empty' "$BACKUP")" \
			"$(jq -r '.VHL_CAM.sd_tmp_path // empty' "$BACKUP")"; do
			[ -n "$D" ] && [ -d "$D" ] &&
				find "$D" -maxdepth 2 -name '*.mp4*' -newermt "@$RUN_T0" -delete 2>/dev/null
		done
		sleep 2
	done
done

# ------------------------------------------------------------------- 요약
log ""
log "### 요약 (combo·노출·채널별 평균 CSI2 fps)"
# CSV 열: 1 case, 2 combo, 3 attempt, 4 req_fps, 5 exp_us, 6 channel, 7 csi_fps.
# 두 가지를 고친다.
#  (1) 합산 열: 도입 시점(b7f7d7d)부터 6열(channel, 즉 "ch0")을 더해 요약이 항상
#      0.00 을 보고했다 — 채널별 줄에는 실제 값이 찍히므로 조용한 거짓이었다.
#  (2) 키: 옛 키 $2" "$5 는 채널을 빼먹어 combo 0+2 가 서로 다른 CSI 둘(ch0=CSI0,
#      ch2=CSI1)을 한 줄로 평균냈다. 120 과 0 이면 60.00 으로 찍혀 머리글의 "채널별"
#      이 거짓이 된다 — 자명하게 망가져 보이던 0.00 보다 알아채기 어렵다.
# 하이픈 가드는 뺐다: 7열은 rate() 의 printf "%.2f" 라 하이픈이 될 수 없고, 하이픈이
# 들어가는 열은 9열(encstat_fps, EF="-")뿐이다. 대신 빈 값을 거른다 — elapsed 가 0
# 이면 rate() 의 나눗셈이 awk 오류로 죽어 7열이 빈 채로 찍히고, 그대로 더하면 0 이
# 섞인다.
awk -F, 'NR>1 && $7!="" { k=$2" "$5" "$6; s[k]+=$7; n[k]++ }
     END { for (k in s) printf "  %-20s %7.2f fps  (n=%d)\n", k, s[k]/n[k], n[k] }' "$CSV" |
	sort | tee -a "$LOG"
log ""
log "=== 측정 종료 $(date -Is) ==="
