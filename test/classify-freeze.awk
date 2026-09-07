# 동결 재분류: 스크립트 라벨을 믿지 않고 원자료로 판정한다.
#
# 사용법:  awk -v NAME=<라벨> -f classify-freeze.awk <per-trial CSV>
#
# 유효 입력은 **한 가지뿐**이다 — 헤더가 정확히
#   t_us,a_total,a_cnt,b_total,b_cnt,csi_d
# 인 회차별 CSV. 이 형식을 쓰는 것은 probe-freeze-fastsample.sh 와 probe-freeze-pollrate.sh
# 의 `fast_*_t<N>.csv` / `poll_*_<arm>_t<N>.csv` 뿐이다.
# probe-freeze-forensics.sh(csi_d 가 3번 필드)와 probe-freeze-exposure.sh(4번 필드)는
# 레이아웃이 달라 **입력으로 쓸 수 없다** — 예전 판은 그런 파일을 먹으면 exit 0 으로
# 그럴듯한 오답을 냈다. 이제는 헤더를 검사하고 거부한다.
#
# 판정:
#   진짜 동결 = 연속 20샘플 이상 정상 스트리밍이 있었고, 이후 CSI 증분이 끝까지 0
#   기동실패  = 지속 스트리밍(연속 20샘플)이 한 번도 없었음
#   완주      = 그 외
# 종료코드: 0 = 판정 출력함, 2 = 입력이 유효하지 않아 판정하지 않음(헤더 불일치·데이터 없음).
BEGIN {
	FS = ","
	run = 0; best = 0; lastnz = -1; n = 0; bad = 0
	EXPECT = "t_us,a_total,a_cnt,b_total,b_cnt,csi_d"
}

NR == 1 {
	line = $0
	sub(/\r$/, "", line)
	if (line != EXPECT) {
		printf "classify-freeze: 지원하지 않는 헤더입니다 (%s)\n", FILENAME > "/dev/stderr"
		printf "  기대: %s\n", EXPECT > "/dev/stderr"
		printf "  실제: %s\n", line > "/dev/stderr"
		printf "  fast_*/poll_* 회차별 CSV 만 입력으로 쓸 수 있습니다.\n" > "/dev/stderr"
		bad = 1
		exit 2
	}
	next
}

$6 != "" {
	n++; t[n] = $1; d[n] = $6 + 0
	if (d[n] > 0) { run++; if (run > best) best = run; lastnz = n } else run = 0
}

$2 != "" { la = $2 + 0; lb = $4 + 0 }

END {
	if (bad) exit 2
	if (n == 0) {
		printf "classify-freeze: 판정할 데이터 행이 없습니다 (%s)\n", FILENAME > "/dev/stderr"
		printf "  잘린 실행과 실제 기동실패를 구분할 수 없으므로 판정하지 않습니다.\n" > "/dev/stderr"
		exit 2
	}
	if (best < 20) { printf "%s,기동실패,,%d,%d\n", NAME, la, lb; exit 0 }
	# lastnz 는 d>0 인 마지막 인덱스이므로, lastnz==n 이면 끝까지 흘렀다는 뜻이고
	# 아니면 그 뒤는 전부 0 이다 — 별도 재검사가 필요 없다(예전 판의 재검사 루프는
	# 이 불변식 때문에 도달 불가능한 죽은 코드였다).
	if (lastnz == n) { printf "%s,완주,,%d,%d\n", NAME, la, lb; exit 0 }
	printf "%s,동결,%d,%d,%d\n", NAME, t[lastnz + 1] / 1000, la, lb
	exit 0
}
