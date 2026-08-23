#!/bin/bash
# run-record-sync-check.sh — 녹화 mp4 4채널의 채널 간 분할 지점 '오차 변화' 를 잰다.
#
# 무엇을 재는가 (그리고 무엇을 못 재는가)
#   한 조각의 프레임 수는 그 채널의 '이전 경계 ~ 현재 경계' 거리다. 따라서 채널마다
#   프레임 수가 같다는 것은 경계가 **같은 만큼 전진했다** = 오차가 변하지 않았다는 뜻이지,
#   오차가 0 이라는 뜻이 아니다. 경계가 [0,900,1800] 인 채널과 [1,901,1801] 인 채널은
#   둘 다 900 프레임 파일을 만든다(1프레임 상수 오프셋이 보이지 않는다).
#
#   따라서 이 도구의 판정은 "구간 내 오차 변화"다. 구간 시작 시점에 이미 존재하던
#   상수 오프셋을 확정하려면 다음 중 하나가 필요하다.
#     - 세션 첫 조각부터 포함해 누적 경계를 세거나
#     - 밀리초 스톱워치를 4채널 동시 촬영해 첫 프레임 표시값을 비교
#
# 왜 프레임 수인가
#   네 채널의 인코더 입력 프레임 타임라인이 동일하다는 것은 온타겟 실측으로 확정돼 있다
#   (docs/CAMERA_SYNC_VALIDATION.md §3.7 §3.8 — 네 videorate 가 동일한 원본 V4L2 sequence 를
#   선택했고 record/rtsp 분기 PTS hash 가 4채널 동일). 그래서 프레임 수 차이를 경계 차이로
#   읽을 수 있다.
#
# 파일 내부 PTS 로는 잴 수 없다
#   splitmuxsink 의 reset-muxer 기본값이 true 이고 이 저장소는 이 값을 바꾸지 않는다.
#   조각마다 타임스탬프가 0 으로 재기준화되어 네 채널 모두 first_pts=0.000000 으로 읽힌다.
#   로컬 재현(tee -> splitmuxsink 2개)과 타겟 실물 양쪽에서 확인했다.
#
# 해상도
#   1프레임(15fps 기준 66.7ms).
#
# 사용법
#   ./run-record-sync-check.sh <fps> <mp4 파일...>
#   EXPECT_CHANNELS="0 1 2 3" ./run-record-sync-check.sh 15 <파일...>
#
#   파일명에서 <공통타임스탬프>-ch<N> 을 인식해 분(minute)별로 묶는다.
#   기대 채널(기본 0 1 2 3)이 입력에 아예 없으면 판정하지 않고 멈춘다 — 3채널만 모아
#   "차이 없음" 이 나오는 오독을 막기 위해서다.
#
#   서비스를 멈추고 재는 경우 /dev/shm -> /mnt/sd_cam 파일 이동은 gstApp 이 아니라 서비스가
#   하므로 /dev/shm 의 '닫힌' .part 를 대상으로 삼는다.
#
# 요구: ffprobe (타겟에 없으면 파일을 ffprobe 가 있는 호스트로 복사해 실행)

set -u

FPS="${1:-15}"
if [ $# -gt 0 ]; then
    shift
fi
EXPECT_CHANNELS="${EXPECT_CHANNELS:-0 1 2 3}"

if [ $# -eq 0 ]; then
    echo "usage: $0 <fps> <mp4 files...>" >&2
    exit 2
fi

if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ERROR: ffprobe 없음. 파일을 ffprobe 가 있는 호스트로 복사해 실행한다." >&2
    exit 3
fi

# 프레임 수는 컨테이너 메타(nb_frames)로 먼저 읽는다 — sample table 에서 오므로 즉시 나온다.
# 일부 컨테이너/muxer 는 nb_frames 를 비워 두는데, 그때만 패킷 전수(-count_packets)로 센다.
# 느리지만 값은 정확하다.
#
# 한계(실측): moov 가 아예 없는 잘린 파일은 두 경로 모두 실패한다("moov atom not found").
# ffprobe 가 demux 자체를 못 하기 때문이며 -count_packets 로도 복구되지 않는다.
# 그런 파일은 SKIP 되고, 아래 기대 채널 검사가 판정을 거부한다 — 일부 채널만으로
# "차이 없음" 을 내놓는 것보다 낫다.
frame_count() {
    local f="$1" n
    n=$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=nb_frames -of csv=p=0 "$f" 2>/dev/null)
    case "$n" in
        ''|*[!0-9]*|0)
            n=$(ffprobe -v error -select_streams v:0 -count_packets \
                -show_entries stream=nb_read_packets -of csv=p=0 "$f" 2>/dev/null)
            ;;
    esac
    printf '%s' "$n"
}

TMP=$(mktemp) || exit 1
trap 'rm -f "$TMP"' EXIT INT TERM

for f in "$@"; do
    if [ ! -f "$f" ]; then
        echo "SKIP (없음): $f" >&2
        continue
    fi
    base=$(basename "$f")
    # {name}_{YYYYmmdd_HHMMSS}-ch{N}.mp4[.part]  ->  stamp, ch
    stamp=$(printf '%s' "$base" | sed -n 's/^.*_\([0-9]\{8\}_[0-9]\{6\}\)-ch[0-9]*\..*$/\1/p')
    ch=$(printf '%s' "$base" | sed -n 's/^.*-ch\([0-9]*\)\..*$/\1/p')
    if [ -z "$stamp" ] || [ -z "$ch" ]; then
        echo "SKIP (이름 형식 불일치): $base" >&2
        continue
    fi

    frames=$(frame_count "$f")
    keys=$(ffprobe -v error -select_streams v:0 -show_entries packet=flags \
           -of csv=p=0 "$f" 2>/dev/null | grep -c '^K')

    case "$frames" in
        ''|*[!0-9]*)
            echo "SKIP (프레임 수 판독 실패): $base" >&2
            continue
            ;;
    esac
    printf '%s %s %s %s\n' "$stamp" "$ch" "$frames" "$keys" >> "$TMP"
done

if [ ! -s "$TMP" ]; then
    echo "판독된 파일 없음" >&2
    exit 4
fi

sort -o "$TMP" "$TMP"

awk -v fps="$FPS" -v expect="$EXPECT_CHANNELS" '
BEGIN { ne = split(expect, EX, /[ ,]+/) }
{ stamp=$1; ch=$2+0; fr=$3; kf=$4
  if (!(stamp in seen)) { order[++n]=stamp; seen[stamp]=1 }
  F[stamp,ch]=fr; K[stamp,ch]=kf; present[ch]=1
}
END {
  # 기대 채널이 입력에 아예 없으면 판정하지 않는다. 관측된 채널만으로 완전성을
  # 따지면 3채널 데이터가 "차이 없음" 으로 나온다.
  missing=""
  for (i=1; i<=ne; i++) { c=EX[i]+0; if (!(c in present)) missing = missing " ch" c }
  if (missing != "") {
    printf "ERROR: 기대 채널이 입력에 없다:%s\n", missing > "/dev/stderr"
    printf "       EXPECT_CHANNELS 로 기대 집합을 바꿀 수 있다 (현재: %s)\n", expect > "/dev/stderr"
    exit 5
  }

  ms = 1000.0/fps
  printf "fps=%s  1프레임=%.2f ms  기대 채널: %s\n\n", fps, ms, expect
  printf "%-16s", "분(minute)"
  for (i=1; i<=ne; i++) printf " %8s", "ch" EX[i]
  printf " %10s %10s\n", "경계변화", "환산(ms)"
  printf "%s\n", "--------------------------------------------------------------------------"

  worst=0; total=0; shifted=0
  for (j=1; j<=n; j++) {
    s=order[j]; lo=-1; hi=-1; miss=0
    printf "%-16s", s
    for (i=1; i<=ne; i++) {
      c=EX[i]+0
      if ((s,c) in F) {
        v=F[s,c]+0; printf " %8d", v
        if (lo<0 || v<lo) lo=v
        if (hi<0 || v>hi) hi=v
      } else { printf " %8s", "-"; miss=1 }
    }
    if (!miss) {
      d=hi-lo
      if (d>worst) worst=d
      printf " %10d %10.1f", d, d*ms
      total++
      if (d>0) shifted++
      # 누적은 모든 기대 채널이 온전한 분만 더한다. 빠진 분을 섞으면 누적이
      # 실제 동기화와 무관하게 벌어져 오독을 만든다.
      for (i=1; i<=ne; i++) { c=EX[i]+0; cum[c]+=F[s,c] }
      cumrows++
    } else printf " %10s %10s", "-", "(불완전)"
    printf "\n"
  }

  printf "\n키프레임 수 (RTSP 접속이 GOP 격자를 흔들었는지 확인)\n"
  printf "%-16s", "분(minute)"
  for (i=1; i<=ne; i++) printf " %8s", "ch" EX[i]
  printf "\n"
  for (j=1; j<=n; j++) {
    s=order[j]; printf "%-16s", s
    for (i=1; i<=ne; i++) {
      c=EX[i]+0
      if ((s,c) in K) printf " %8d", K[s,c]; else printf " %8s", "-"
    }
    printf "\n"
  }

  printf "\n누적 프레임 수 (완전한 분 %d개 기준 — 경계가 함께 움직였는지)\n", cumrows
  for (i=1; i<=ne; i++) { c=EX[i]+0; printf "  ch%-3d %12d\n", c, cum[c] }

  printf "\n판정: 완전한 분 %d개 중 경계 변화가 있는 분 %d개\n", total, shifted
  printf "      최대 변화 %d 프레임 = %.1f ms\n", worst, worst*ms
  if (total == 0)
    printf "      -> 모든 기대 채널이 갖춰진 분이 없어 판정할 수 없다\n"
  else if (worst==0)
    printf "      -> 측정 구간에서 채널 간 분할 지점 오차가 변하지 않았다\n"
  else
    printf "      -> 채널 간 분할 지점 오차가 변했다. 위 표에서 어느 채널이 언제 밀렸는지 확인한다\n"

  printf "\n주의: 이 도구가 재는 것은 오차의 변화다. 구간 시작 시점에 이미 있던\n"
  printf "      상수 오프셋(예: 네 채널이 나란히 1프레임씩 어긋난 채 유지)은 검출되지 않는다.\n"
  printf "      절대 오차를 확정하려면 세션 첫 조각부터 포함하거나, 밀리초 스톱워치를\n"
  printf "      4채널 동시 촬영해 첫 프레임 표시값을 비교해야 한다.\n"
  printf "      해상도는 1프레임(%.2f ms)이다.\n", ms
}' "$TMP"
