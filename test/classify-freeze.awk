# 동결 재분류: 스크립트 라벨을 믿지 않고 원자료로 판정한다.
#  진짜 동결 = 연속 20샘플 이상 정상 스트리밍이 있었고, 이후 CSI 증분이 끝까지 0
#  기동실패  = 지속 스트리밍(연속 20샘플)이 한 번도 없었음
#  완주      = 그 외
BEGIN{ FS=","; run=0; best=0; lastnz=-1; n=0 }
NR>1 && $6!="" {
  n++; t[n]=$1; d[n]=$6+0
  if (d[n]>0) { run++; if(run>best)best=run; lastnz=n } else run=0
}
NR>1 && $2!="" { at=$2+0; bt=$4+0; la=at; lb=bt }
END{
  if (best < 20) { printf "%s,기동실패,,%d,%d\n", NAME, la, lb; exit }
  if (lastnz == n) { printf "%s,완주,,%d,%d\n", NAME, la, lb; exit }
  # 마지막 비영 이후가 전부 0 인지 확인
  for (k=lastnz+1; k<=n; k++) if (d[k]>0) { printf "%s,완주,,%d,%d\n", NAME, la, lb; exit }
  printf "%s,동결,%d,%d,%d\n", NAME, t[lastnz+1]/1000, la, lb
}
