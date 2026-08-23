#!/bin/bash
# run-record-sync-check.sh — 녹화 mp4 4채널의 채널 간 분할 지점 오차를 잰다.
#
# 원리
#   네 채널의 인코더 입력 프레임 타임라인이 동일하다는 것은 온타겟 실측으로 확정돼 있다
#   (docs/CAMERA_SYNC_VALIDATION.md §3.7 §3.8 — 네 videorate 가 동일한 원본 V4L2 sequence 를
#   선택했고, record/rtsp 분기의 PTS hash 가 4채널 동일). 따라서 같은 분(minute)의 파일 4개에
#   담긴 프레임 수가 다르면 그 차이가 곧 그 경계에서의 분할 지점 차이다. 1프레임 = 1/fps 초.
#
# 파일 내부 PTS 로는 잴 수 없다
#   splitmuxsink 의 reset-muxer 기본값이 true 이고 이 저장소는 이 값을 바꾸지 않는다.
#   조각마다 타임스탬프가 0 으로 재기준화되므로 네 채널 모두 first_pts=0.000000 으로 읽힌다.
#   (로컬 재현과 타겟 실물 양쪽에서 확인)
#
# 해상도
#   1프레임(15fps 기준 66.7ms). 그보다 작은 어긋남과 "실제 장면이 몇 ms 다른가" 는
#   밀리초 스톱워치를 4채널 동시 촬영해 첫 프레임의 표시값을 비교해야 확정된다.
#
# 사용법
#   ./run-record-sync-check.sh <fps> <mp4 파일...>
#     예) ./run-record-sync-check.sh 15 /mnt/sd_cam/VD3001_20260823_09*-ch?.mp4
#
#   파일명에서 <공통타임스탬프>-ch<N> 을 인식해 분(minute)별로 묶는다.
#   운영 중 서비스를 멈추고 재려는 경우, /dev/shm -> /mnt/sd_cam 파일 이동은 gstApp 이 아니라
#   서비스가 하므로 /dev/shm 의 '닫힌' .part 파일을 대상으로 삼는다.
#
# 요구: ffprobe (타겟에 없으면 파일을 ffprobe 가 있는 호스트로 복사해 실행)

set -u

FPS="${1:-15}"
shift 2>/dev/null || true

if [ $# -eq 0 ]; then
    echo "usage: $0 <fps> <mp4 files...>" >&2
    exit 2
fi

if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ERROR: ffprobe 없음. 파일을 ffprobe 가 있는 호스트로 복사해 실행한다." >&2
    exit 3
fi

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

    frames=$(ffprobe -v error -select_streams v:0 \
             -show_entries stream=nb_frames -of csv=p=0 "$f" 2>/dev/null)
    keys=$(ffprobe -v error -select_streams v:0 -show_entries packet=flags \
           -of csv=p=0 "$f" 2>/dev/null | grep -c '^K')
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f" 2>/dev/null)

    case "$frames" in
        ''|*[!0-9]*)
            echo "SKIP (프레임 수 판독 실패): $base" >&2
            continue
            ;;
    esac
    printf '%s %s %s %s %s\n' "$stamp" "$ch" "$frames" "$keys" "$dur" >> "$TMP"
done

if [ ! -s "$TMP" ]; then
    echo "판독된 파일 없음" >&2
    exit 4
fi

sort -o "$TMP" "$TMP"

awk -v fps="$FPS" '
{ stamp=$1; ch=$2; fr=$3; kf=$4
  if (!(stamp in seen)) { order[++n]=stamp; seen[stamp]=1 }
  F[stamp,ch]=fr; K[stamp,ch]=kf
  if (ch+0 > maxch) maxch=ch+0
  chs[ch+0]=1
}
END {
  ms = 1000.0/fps
  printf "fps=%s  1프레임=%.2f ms\n\n", fps, ms
  printf "%-16s", "분(minute)"
  for (c=0; c<=maxch; c++) if (c in chs) printf " %8s", "ch" c
  printf " %10s %10s\n", "최대-최소", "환산(ms)"
  printf "%s\n", "--------------------------------------------------------------------------"

  worst=0; total=0; skewed=0
  for (i=1; i<=n; i++) {
    s=order[i]; lo=-1; hi=-1; miss=0
    printf "%-16s", s
    for (c=0; c<=maxch; c++) {
      if (!(c in chs)) continue
      if ((s,c) in F) {
        v=F[s,c]+0; printf " %8d", v
        if (lo<0 || v<lo) lo=v
        if (hi<0 || v>hi) hi=v
      } else { printf " %8s", "-"; miss=1 }
    }
    if (lo>=0 && hi>=0 && !miss) {
      d=hi-lo
      if (d>worst) worst=d
      printf " %10d %10.1f", d, d*ms
      total++
      if (d>0) skewed++
    } else printf " %10s %10s", "-", "(불완전)"
    printf "\n"
    for (c=0; c<=maxch; c++) if ((s,c) in F) cum[c]+=F[s,c]
  }

  printf "\n키프레임 수 (RTSP 접속이 GOP 격자를 흔들었는지 확인)\n"
  printf "%-16s", "분(minute)"
  for (c=0; c<=maxch; c++) if (c in chs) printf " %8s", "ch" c
  printf "\n"
  for (i=1; i<=n; i++) {
    s=order[i]; printf "%-16s", s
    for (c=0; c<=maxch; c++) {
      if (!(c in chs)) continue
      if ((s,c) in K) printf " %8d", K[s,c]; else printf " %8s", "-"
    }
    printf "\n"
  }

  printf "\n누적 프레임 수 (어긋난 분할 지점이 상쇄되는지)\n"
  for (c=0; c<=maxch; c++) if (c in chs) printf "  ch%-3d %12d\n", c, cum[c]

  printf "\n판정: 완전한 분 %d개 중 채널 간 프레임 수가 다른 분 %d개\n", total, skewed
  printf "      최대 차이 %d 프레임 = %.1f ms\n", worst, worst*ms
  if (worst==0)
    printf "      -> 측정 구간에서 채널 간 분할 지점 차이 없음 (프레임 단위 해상도)\n"
  else
    printf "      -> 채널 간 분할 지점이 어긋남. 위 표에서 어느 채널이 언제 밀렸는지 확인한다\n"
  printf "\n주: 해상도는 1프레임(%.2f ms)이다. 그보다 작은 어긋남과 실제 장면 차이는\n", ms
  printf "    밀리초 스톱워치 4채널 동시 촬영으로만 확정된다.\n"
}' "$TMP"
