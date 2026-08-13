# RTSP 다채널 동기화 클라이언트 구현 요구사항

- 최종 갱신: 2026-08-13
- 제품 client 기준 환경: Windows, 4채널 FHD 15fps, H.265, RTSP/TCP

## 1. 문서 목적

이 문서는 `gstApp`의 4채널 RTSP 영상을 수신하는 제품 클라이언트에서 필요한
프레임 동기화, 장애 복구, queue 관리 및 decoder 정책을 정리한다.

근거가 되는 전체 시험 이력은 `docs/CAMERA_SYNC_VALIDATION.md`에 있으며, 이
문서는 클라이언트 구현 항목만 분리한 것이다.

## 2. 적용 범위와 전제

- 입력: `/ch0`~`/ch3`, 독립 RTSP/TCP session
- 현재 검증 codec: H.265
- 기준 frame rate: 15fps
- 동일 원본 프레임 식별자: H.265 SEI의 `GSTSYNC1` Frame ID
- 각 RTSP session의 PTS와 RTP timestamp는 서로 독립적이다.
- Frame ID SEI는 현재 시험용 계측 server에서만 활성화된다. 원본 운영
  binary에는 해당 기능이 포함되지 않았으므로 제품 적용 전 server 기능을
  정식 반영해야 한다.

## 3. 측정으로 확인된 특성

### 3.1 정상 외부망

전용 client `192.168.214.8`에서 30초 동안 측정한 결과다.

- 공통 Frame ID group: 444개, ID 98..541
- gap/duplicate/invalid SEI/group mismatch: 0
- 초기 channel 차이: 최대 3frame
- 동일 Frame ID 도착 spread: 평균 11.730ms, 최대 38.826ms
- 독립 client PTS spread: 최대 112.914ms

따라서 같은 원본 프레임 판정에 RTSP PTS를 사용하면 안 되며 Frame ID를
사용해야 한다.

동일 환경의 장시간 SEI ON 시험에서도 다음 결과를 확인했다.

| 시험 | 공통 Frame ID group | 오류 | 동일-ID 도착 spread 평균/최대 |
| --- | ---: | --- | ---: |
| 30분 | 27,298 | gap/duplicate/invalid/mismatch 0 | 12.589/89.939ms |
| 역순 A/B의 ON 10분 | 9,302 | gap/duplicate/invalid/mismatch 0 | 12.593/83.461ms |

장시간 시험의 최대 spread는 15fps 한 frame 주기 66.667ms를 넘었다. 그러나
Frame ID와 원본 PTS는 일치했으므로 원본 frame mismatch가 아니라 독립
RTSP/TCP session의 순간적인 도착 시각 차이다.

### 3.2 packet loss

RTSP/TCP에서 packet loss 0.1%, 0.5%, 1.0%를 적용했을 때 TCP 재전송으로
Frame ID gap은 발생하지 않았다. 그러나 1% 조건의 동일 Frame ID 최대 도착
spread는 424.967ms까지 증가했다.

### 3.3 bandwidth 부족

정상 4채널 전송량 약 15Mbps를 제한한 결과다.

| 제한 | Frame ID channel gap 합계 | alignment drop 합계 | 최대 도착 spread |
| --- | ---: | ---: | ---: |
| 12Mbps | 24 | 56 | 8.894s |
| 8Mbps | 107 | 181 | 18.941s |

연결은 유지되더라도 server의 leaky queue가 오래된 frame을 폐기하고 channel별
진행 위치가 크게 달라질 수 있다.

## 4. 필수 수신 구조

각 channel은 독립 pipeline과 lifecycle을 가져야 한다.

```text
rtspsrc
  → rtph265depay
  → h265parse
  → encoded AU/SEI 처리
  → decoder
  → renderer 또는 decoded appsink
```

권장 parser 출력은 다음과 같다.

```text
video/x-h265,stream-format=byte-stream,alignment=au
```

한 channel의 재접속 때문에 다른 channel pipeline을 재생성하지 않는다. 다만
공통 동기화 epoch와 미완성 group은 전체 channel 기준으로 갱신해야 한다.

## 5. Frame ID SEI 처리

### 5.1 필수 검증 항목

AU에서 다음 값을 읽고 검증한다.

- magic: `GSTSYNC1`
- payload version
- payload channel ID
- 64bit Frame ID
- 64bit 원본 PTS
- payload 길이

H.265 Annex-B의 3byte/4byte start code와 emulation-prevention byte를 모두
처리해야 한다.

```text
00 00 01
00 00 00 01
```

### 5.2 오류 처리

다음 frame은 동기화 group에 넣지 않는다.

- SEI 누락 또는 payload 길이 오류
- 지원하지 않는 version
- mount channel과 payload channel 불일치
- 이전 ID 이하의 duplicate/역행 ID

오류는 channel별 `invalid_sei`, `duplicate`, `backwards_id`로 분리 집계한다.

## 6. 시작 barrier

초기 접속 시 각 channel의 첫 Frame ID는 최대 수 frame 다를 수 있다. 네 queue의
선두 ID가 같아질 때까지 오래된 frame을 폐기한 뒤 첫 공통 ID에서 barrier를
성립시킨다.

개념 알고리즘:

```text
while 모든 channel queue가 비어 있지 않음:
    target = 네 queue 선두 Frame ID 중 최댓값
    각 queue에서 target보다 작은 frame 폐기
    네 queue 선두가 모두 target이면 공통 group 생성
```

시작 과정의 폐기는 `initial_drop`으로 기록하고 운용 중 폐기와 구분한다.

## 7. 운용 중 공통 group 생성

한 group은 다음 조건을 모두 만족해야 한다.

- 네 channel Frame ID가 동일
- payload channel ID가 실제 channel과 일치
- 네 channel의 원본 PTS가 동일
- 같은 epoch에 속함

한 channel에서 ID가 누락되면 나머지 channel의 같은 ID도 renderer에 전달하지
않거나, 제품 요구에 따라 명시적인 incomplete group으로 전달해야 한다. 서로
다른 Frame ID를 가까운 PTS라는 이유로 같은 group으로 묶으면 안 된다.

## 8. Queue와 latency 정책

### 8.1 개수 상한만 사용하면 안 되는 이유

외부망 8Mbps 시험에서 빠른 channel queue에 251frame이 남았으며, 최대 도착
spread는 18.941초였다. `max_queue_frames=300`만 적용하면 15fps 기준 최대
20초의 과거 영상이 유지될 수 있다.

### 8.2 필수 상한

제품 client에는 다음 두 상한이 모두 필요하다.

- frame 개수 상한
- 가장 오래된 frame의 대기 시간 상한

초기 권장값은 제품 latency 요구에 맞춰 조정하되 다음 범위에서 시작한다.

```text
max_queue_frames: 15~30frame
max_group_wait: 500~1000ms
```

이는 확정 제품값이 아니라 실측을 시작하기 위한 권장 초기값이다.

### 8.3 Timeout 발생 시

한 channel의 목표 ID가 `max_group_wait` 안에 도착하지 않으면:

1. 해당 ID의 incomplete group을 폐기한다.
2. 네 channel queue에서 해당 ID 이하를 정리한다.
3. 현재 수신된 가장 최신의 공통 가능 ID로 barrier를 다시 계산한다.
4. timeout이 연속되면 장애 channel을 재접속한다.

## 9. 재접속과 epoch

기존 `rtspsrc` element만 `NULL→PLAYING`으로 재사용하는 방식은 dynamic pad
`not-linked` 오류가 발생했다. 대상 channel pipeline 전체를 제거하고 다시
생성해야 한다.

재접속 절차:

1. 장애 channel pipeline을 `NULL`로 전환하고 제거
2. 전체 미완성 queue 폐기
3. epoch 증가
4. channel pipeline 새로 생성
5. 새 channel의 첫 Frame ID 수신
6. 네 channel 공통 barrier 재수립

재접속 동안의 ID 공백은 `reconnect_skipped_ids`로 기록하고 비의도적 network
gap과 구분한다.

재접속 후 독립 RTSP PTS는 다시 0 근처에서 시작할 수 있으므로 이전 session
PTS와 직접 비교하면 안 된다.

## 10. Decoder 정책

- H.265 IDR NAL type 19/20을 감시한다.
- decoder bus의 `ERROR`, `WARNING`, `EOS`를 반드시 처리한다.
- Frame ID gap 직후 delta frame부터 수신될 수 있다.
- decoder가 정상 frame을 출력하지 않으면 다음 IDR을 기다린다.
- IDR 대기 timeout을 넘으면 channel pipeline을 재생성한다.

화면 복구 시간은 다음 시각을 분리 기록해야 한다.

```text
gap 또는 reconnect 종료
→ 첫 IDR AU 수신
→ 첫 decoded buffer
→ 첫 rendered frame
```

## 11. 네트워크 장애 대응

### 11.1 TCP packet loss

1% packet loss에서도 Frame ID gap은 없었지만 최대 도착 spread가 약 425ms로
증가했다. packet loss가 없다는 이유로 실시간성이 정상이라고 판단하면 안 된다.

### 11.2 대역폭 부족

다음 징후를 동시에 감시한다.

- Frame ID gap 증가
- queue depth 증가
- oldest frame age 증가
- alignment drop 증가
- channel 간 최신 Frame ID 차이 증가

임계값 초과 시 과거 frame을 빠르게 소비하지 말고 폐기한 뒤 최신 공통 ID로
재정렬해야 한다.

## 12. 필수 상태와 통계

channel별:

- 연결 상태 및 epoch
- 마지막 수신 Frame ID
- gap event/누락 ID 수
- duplicate/invalid SEI
- queue depth와 oldest age
- reconnect 횟수 및 skipped ID
- 첫 IDR/decoded/rendered 시각

전체 동기화기:

- barrier ID와 barrier 횟수
- 공통 group 수
- group mismatch
- initial/alignment/incomplete drop
- 동일-ID 도착 spread 최소/평균/P95/P99/최대
- 현재 channel 간 최신 Frame ID 범위

## 13. 권장 상태 머신

```text
DISCONNECTED
  → CONNECTING
  → WAITING_FIRST_FRAME
  → WAITING_BARRIER
  → SYNCHRONIZED
  → DEGRADED        (gap/timeout/queue 증가)
  → RECONNECTING    (연속 timeout 또는 decoder 오류)
  → WAITING_BARRIER (새 epoch)
```

`DEGRADED` 상태에서도 서로 다른 ID를 강제로 group으로 만들지 않는다.

## 14. 제품 적용 전 합격 기준

최소 합격 기준은 다음과 같다.

1. 정상망 30분 이상에서 group mismatch/duplicate/invalid SEI 0
2. 시작 시 최대 수 frame 차이를 barrier로 자동 흡수
3. 단일 channel 3회 반복 재접속 후 매회 barrier 재수립
4. packet loss 1%에서 연결 유지 또는 명시적 재접속
5. 대역폭 부족 시 queue age가 제품 상한을 넘지 않음
6. incomplete group을 다른 Frame ID와 잘못 결합하지 않음
7. decoder 오류/IDR timeout 후 자동 복구
8. memory 사용량이 장시간 일정 범위 안에 유지

## 15. 구현 우선순위

1. Frame ID SEI parser와 시작 barrier
2. frame 개수+시간 기반 bounded queue
3. incomplete group timeout과 최신 ID 재정렬
4. channel 독립 pipeline 재생성 및 epoch
5. decoder IDR/error 복구
6. 통계 및 장애 로그
7. 장시간/부하/네트워크 회귀시험 자동화

## 16. Windows client 기술 구성

### 16.1 권장 구성

동일 원본 frame을 실제 화면에서도 맞춰야 하는 제품 모드는 FFmpeg library를
기반으로 구현한다.

```text
/ch0~ch3 독립 RTSP/TCP session
  → libavformat 수신 및 depacketize
  → H.265 AU 경계 확정
  → GSTSYNC1 SEI 해석
  → compressed AU bounded queue
  → 공통 Frame ID barrier/group 생성
  → libavcodec decode
  → Direct3D11 render
```

- `ffplay`는 FFmpeg/SDL 기반의 독립 실행 player이므로 연결, codec, latency
  확인용으로 사용하고 제품 application에 직접 포함하지 않는다.
- LibVLC는 빠른 4채널 모니터 화면 구현에는 적합하지만 일반적인 decoded video
  callback 시점에는 압축 AU의 custom SEI를 이용한 공통 barrier를 만들기 어렵다.
- 따라서 제품을 다음 두 mode로 구분할 수 있다.
  - `MONITOR`: LibVLC 기반 독립 4채널 재생, frame alignment 보장 없음
  - `FRAME_SYNC`: FFmpeg 기반 SEI parser/barrier/decode/render, 제품 권장
- FFmpeg demux 결과가 항상 한 `AVPacket`과 한 AU의 일대일 관계라고 가정하지
  않는다. H.265 parser를 통해 AU를 확정한 뒤 SEI를 검사한다.
- queue에는 가능한 한 decoded frame이 아니라 compressed AU를 보관한다.

### 16.2 Windows component 경계

```text
RtspChannelReceiver[4]   RTSP 연결, timeout, reconnect
H265SeiFrameIdParser    payload 검증 및 Frame ID 추출
FrameSynchronizer       barrier, group, timeout, epoch
ChannelDecoder[4]       선택된 AU decode와 IDR 복구
VideoRenderer           Direct3D11 texture 출력
RecoveryController      장애 판정 및 channel 독립 재생성
SyncMetrics             queue, gap, spread, 자원 통계
```

RTSP callback/thread에서는 packet 전달과 최소 검증만 수행한다. decode, 화면 출력,
파일 기록 또는 긴 lock 대기는 수신 thread에서 실행하지 않는다.

## 17. 제품 server 요구사항

### 17.1 필수 변경

정밀 동기화 mode는 server가 동일 원본 frame의 식별자를 제공해야 동작한다.
현재 작업 트리에는 H.265 SEI prototype이 있지만 운영 target에 복구된 원본
binary에는 포함되지 않았다.

제품 server에는 다음 항목을 반영한다.

1. `GSTAPP_RTSP_FRAME_ID_SEI=1` 시험 환경변수 의존성을 제품 설정으로 전환
2. H.265 RTSP AU마다 `GSTSYNC1` payload 삽입
3. `gstreamer-codecparsers-1.0` build/runtime 의존성 반영
4. channel별 SEI 삽입 성공/실패와 RTSP queue drop을 상시 집계
5. 기능 활성 상태와 payload version을 시작 log에 기록
6. server/pipeline 재시작을 구분할 `server_epoch` 또는 `generation` 추가

현재 version 1 payload는 다음과 같다.

| Offset | 크기 | 값 |
| ---: | ---: | --- |
| 0 | 8 | `GSTSYNC1` |
| 8 | 1 | version `1` |
| 9 | 1 | channel ID |
| 10 | 2 | reserved |
| 12 | 8 | big-endian Frame ID |
| 20 | 8 | big-endian 원본 PTS |

제품 version에서는 재시작 전후의 같은 숫자 Frame ID를 구분하도록 동기화 key를
`{server_epoch, frame_id}`로 확장하는 것이 안전하다. payload를 변경하면 version을
올리고 기존 version 1 client와의 호환 정책을 명시한다.

### 17.2 유지할 기존 동작

- SEI는 encoder 뒤 RTSP용 buffer에만 추가한다. record branch와 encoder 입력은
  변경하지 않는다.
- 원본 PTS 제거와 RTSP media별 `do-timestamp=1`은 유지한다. 원본 PTS는 SEI
  payload로 전달한다.
- 네 RTSP session은 독립적으로 시작한다. server에서 네 client 접속을 기다리는
  공통 시작 barrier를 만들지 않는다.
- queue full 뒤 IDR 요청과 keyframe까지 delta frame을 제외하는 기존 복구
  정책을 유지한다.
- server leaky queue는 실시간성을 위해 유지하되 overrun/drop을 반드시
  외부에서 관측할 수 있어야 한다.

### 17.3 선택 변경

- H.264가 제품 범위에 포함되면 H.264 SEI 삽입과 client parser가 별도로 필요하다.
- 센서 노출 frame까지 추적하려면 AP1302/driver frame metadata를 영상 buffer와
  결합해 payload에 추가해야 한다. 현재 Frame ID는 V4L2 이후 공통 PTS lineage를
  나타내며 센서 노출 시각을 직접 나타내지 않는다.

## 18. 실제 동작과 영향 분석

이 절은 코드/packet 크기로 계산한 값과 2026-08-13 server A/B 실측값을
구분한다. server 결과는 4채널 FHD 15fps H.265 RTSP/TCP 환경의 값이며,
Windows 제품 client 자원값과 4채널 30fps server 값은 아직 측정하지 않았다.

### 18.1 server 동작

frame마다 다음 처리가 encoder 이후 RTSP branch에서 실행된다.

```text
공통 origin PTS 읽기
  → Frame ID 계산
  → 28byte payload와 H.265 prefix SEI NAL 생성
  → 기존 encoded AU에 SEI 삽입
  → 기존 PTS를 제거한 RTSP용 buffer를 appsrc에 push
  → 독립 RTSP PTS/RTP timestamp 생성
```

이는 영상을 다시 encode하지 않으므로 VPU encode 횟수와 record bitstream에는
영향을 주지 않는다. 60초 실측에서는 채널별 900 AU, 전체 3,600 AU의 SEI 삽입이
모두 성공했고 추가 drop, PTS 역행 및 RTP timestamp 역행은 0이었다. 기존
GStreamer H.265 client도 계속 수신했다.

### 18.2 network 증가량

실제 `rtsp_frame_id_final.pcapng`에서 이번 28byte payload는 emulation-prevention과
NAL header를 포함한 40byte H.265 SEI RTP payload가 됐다. 이번 RTSP/TCP
capture에서는 frame마다 다음 크기의 별도 interleaved RTP 단위로 관찰됐다.

```text
H.265 SEI RTP payload   40byte
RTP header              12byte
RTSP interleaved header  4byte
합계                    56byte/frame
```

Ethernet/IP/TCP header와 ACK를 제외한 계산은 다음과 같다.

| 설정 | 전체 frame/s | 추가량 | 약 15Mbps 대비 |
| --- | ---: | ---: | ---: |
| 4채널 15fps | 60 | 3,360B/s = 26.88kbps | 약 0.18% |
| 4채널 30fps | 120 | 6,720B/s = 53.76kbps | 약 0.36% |

TCP가 여러 RTP 단위를 한 packet에 합칠 수 있으므로 실제 L2/L3 overhead는 packet
batching에 따라 달라진다. 따라서 위 값은 SEI 기능 자체의 application 전송량이다.

실제 server NIC의 송신량은 동일 binary에서 기능만 OFF/ON한 두 A/B 시험에서
다음과 같이 증가했다.

| 시험 순서 | OFF | ON | 증가량 |
| --- | ---: | ---: | ---: |
| OFF 30분 → ON 30분 | 14.431Mbps | 14.521Mbps | 89.889kbps |
| ON 10분 → OFF 10분 | 14.560Mbps | 14.621Mbps | 61.451kbps |

관측 증가량은 두 시험 모두 0.1Mbps 미만이다. 계산값 26.88kbps보다 큰 차이에는
live encoder의 장면별 bitrate 변동과 TCP packet batching 변화가 포함된다.

### 18.3 server CPU와 memory

- 4채널 15fps에서는 초당 60회의 Frame ID 계산과 SEI 삽입을 수행한다.
- 영상 재압축은 하지 않지만 `GArray`, SEI `GstMemory`, 결과 `GstBuffer` 생성과
  H.265 AU parser 처리가 frame마다 발생한다.
- 영구 상태는 channel별 parser와 counter 정도이고, frame별 SEI memory는 약
  수십 byte와 buffer descriptor다.
- 실제 memory 사용량은 SEI 자체보다 `appsink`, `appsrc`, factory queue가 보관하는
  encoded AU 크기의 영향을 더 크게 받는다.
- frame별 log를 남기면 SEI 처리보다 log I/O가 더 큰 부하가 될 수 있으므로 제품
  통계는 counter 누적과 저주기 요약 log를 사용한다.

동일 binary에서 SEI 기능만 OFF/ON한 30분 시험과 순서를 뒤집은 10분 시험의
결과는 다음과 같다. `pidstat`의 process CPU 100%는 CPU core 하나를 뜻한다.

| 항목 | OFF 30분 → ON 30분 | ON 10분 → OFF 10분 | 판정 |
| --- | ---: | ---: | --- |
| process CPU ON-OFF | +1.925%p | +2.020%p | 순서와 무관하게 재현 |
| system CPU ON-OFF | +0.630%p | +0.518%p | 순서와 무관하게 재현 |
| 평균 PSS ON-OFF | +6.146MiB | -0.698MiB | 고정 증가로 재현 안 됨 |

process CPU 증가는 한 core 기준 약 +1.93~2.02%p이며, 4-core 전체 capacity로
환산하면 약 +0.48~0.51%p다. PSS 차이는 시험 순서를 바꾸자 방향도 바뀌었으므로
SEI의 고정 memory 비용으로 판정하지 않는다. 다만 재시작 후 30분 동안 PSS가
OFF +19.486MiB, ON +21.215MiB 증가했으므로 수시간 plateau/leak 시험은 남아 있다.

SEI 생성/삽입 함수 구간 7,200회의 수행시간은 평균 86.877us, 최소 32.251us,
최대 1.326793ms였다. 78.49%가 100us 이하, 99.35%가 250us 이하였다. 평균값과
초당 60회를 곱하면 해당 함수 구간은 한 core의 약 0.52%에 해당한다. 전체
process A/B의 약 +2.0%p에는 allocator, 추가 RTP payload 처리 및 측정 변동도
포함된다. timing 시험에서도 7,200회 삽입이 모두 성공했고 삽입 실패, queue
overrun 및 push failure는 0이었다.

### 18.4 동기화로 추가되는 client 지연

SEI는 frame identity를 추가할 뿐 frame을 기다리지 않으므로 server에 의도적인
buffering latency를 추가하지 않는다. 실사용에서 추가되는 지연은 Windows
client가 네 channel의 같은 Frame ID를 기다리는 시간이다.

| 조건 | 실측 동일-ID 도착 spread/영향 |
| --- | --- |
| 전용 외부 client 정상망 | 평균 11.730ms, 최대 38.826ms |
| 정상망 30분 | 평균 12.589ms, 최대 89.939ms |
| 정상망 역순 A/B의 ON 10분 | 평균 12.593ms, 최대 83.461ms |
| 시작 시 session 준비 차이 | 최대 3frame, 15fps 기준 200ms 구간 정렬 폐기 |
| RTSP/TCP packet loss 1% | 최대 424.967ms |
| 전체 12Mbps 제한 | 최대 8.894s, server channel drop 발생 |
| 전체 8Mbps 제한 | 최대 18.941s, server/client queue 누적 |

정상망에서는 가장 먼저 도착한 channel frame이 평균 약 11.7~12.6ms 동안 나머지
channel을 기다렸다. 짧은 시험 최대값은 38.826ms였지만 장시간 시험에서는
83.461~89.939ms로 한 frame 주기를 넘었다. 따라서 정상망 timeout을 짧은 시험의
최대값이나 66.667ms보다 작게 고정하면 정상 frame group도 불필요하게 폐기할 수
있다. 시작 시 최대 3frame 차이는 과거 frame을 빠르게 재생하는 것이 아니라
버리고 첫 공통 ID에서 시작한다.

대역폭 부족 상태에서 frame 개수 상한만 사용하면 수 초의 과거 영상이 남을 수
있다. `max_group_wait=500~1000ms` 시간 상한을 적용하면 그 이상 기다리지 않고
불완전 group을 폐기하므로 지연은 제한되지만 화면 frame drop이 증가한다.

이 값은 **동기화 대기시간**이며 카메라 노출부터 Windows 화면까지의 전체
glass-to-glass latency는 아직 측정하지 않았다. 전체 latency에는 capture,
encoder, server/RTSP queue, network, decoder와 renderer가 추가된다.

### 18.5 Windows client memory

권장 구조처럼 decode 전 compressed AU를 보관하면 정상 총 전송률 약 15Mbps에서
전체 4채널 queue payload의 단순 계산은 다음과 같다.

| 보관 시간 | compressed payload 단순 계산 |
| ---: | ---: |
| 500ms | 약 0.94MB |
| 1초 | 약 1.88MB |
| 2초 | 약 3.75MB |

실제 사용량에는 packet/object overhead, IDR 크기, decoder reference surface와
render texture가 추가된다.

FHD decoded frame을 queue에 오래 보관하면 영향이 급격히 커진다.

| pixel format | FHD 1frame | 4채널 1group | 채널별 15frame 보관 | 채널별 30frame 보관 |
| --- | ---: | ---: | ---: | ---: |
| NV12 | 약 2.97MiB | 약 11.87MiB | 약 178MiB | 약 356MiB |
| BGRA | 약 7.91MiB | 약 31.64MiB | 약 475MiB | 약 949MiB |

따라서 barrier queue는 compressed AU로 유지하고, 공통 group으로 선택된 AU만
decode한다. decoder reference surface와 화면용 texture는 별도의 작은 순환
buffer로 관리한다.

### 18.6 Windows CPU/GPU

- SEI 검색과 Frame ID 비교는 compressed data 처리이므로 주요 부하는 아니다.
- 주 부하는 4개 FHD H.265 decoder와 색공간 변환 및 화면 합성이다.
- 제품에서는 FFmpeg D3D11VA hardware decode와 Direct3D11 texture 경로를 우선
  검증한다.
- software decode fallback은 기능 확인용으로 제공할 수 있지만 실제 Windows
  장치 CPU에서 4채널 15fps를 장시간 유지하는지는 별도 측정해야 한다.
- hardware decode 사용 여부와 관계없이 SEI는 decoder에 넣기 전 compressed AU에서
  추출해야 한다.

## 19. 제품 적용 전 자원 A/B 시험

### 19.1 server

같은 장면과 동일한 client 수를 유지한 상태에서 SEI OFF/ON을 각각 측정한다.
2026-08-13 현재 완료 여부는 다음과 같다.

- 완료: 4채널 FHD 15fps OFF/ON 30분 및 역순 10분 A/B
- 완료: process CPU 평균/P95/최대와 전체 system CPU
- 완료: PSS 시작/평균/최대/종료 비교
- 완료: frame별 SEI 삽입 수행시간 평균/최소/최대 및 구간별 histogram
- 완료: 송신 byte 증가량 비교
- 완료: `frame_id_sei_failed`, queue overrun, drop, push failure 확인
- 완료: CPU/SoC 온도 비교. 순서 반전 시 온도 차이는 재현되지 않음
- 미완료: 4채널 FHD 30fps stress 조건
- 미완료: 수시간 RSS/PSS plateau 및 시간당 증가량
- 미완료: packet 수와 throttling counter의 별도 장시간 비교

### 19.2 Windows client

- FFmpeg software decode와 D3D11VA를 분리 측정
- process CPU, private working set, GPU Video Decode/3D 사용률
- channel별 compressed queue byte/frame/oldest age
- 동일-ID wait P50/P95/P99/최대
- decode와 render 완료시간
- 30분 및 장시간 memory 증가량
- 정상망, loss 1%, 12Mbps/8Mbps, 단일 channel 반복 reconnect

4채널 15fps SEI server의 CPU, PSS, 송신량과 삽입시간은 실측값을 사용한다.
Windows decoder/GPU 자원, 30fps server 및 수시간 memory 안정성은 측정 전까지
예상치로만 관리하며 제품 합격 판정에 사용하지 않는다.

## 20. 관련 파일

- 검증 client: `test/rtspFrameSyncClient.cpp`
- decoder 검증 client: `test/decoderRecoveryClient.cpp`
- server Frame ID/RTSP 계측: `rtspServerBin.cpp`, `rtspServerBin.h`
- 전체 시험 기록: `docs/CAMERA_SYNC_VALIDATION.md`
- 외부망 종합 분석:
  `tmp/target-192.168.214.4/rtsp_external_impairment_analysis.txt`
- SEI packet capture:
  `tmp/target-192.168.214.4/rtsp_frame_id_final.pcapng`
- SEI 분석:
  `tmp/target-192.168.214.4/rtsp_frame_id_final_analysis.txt`
- SEI 자원/삽입시간 종합 분석:
  `tmp/target-192.168.214.4/rtsp_sei_resource_combined_analysis.txt`
- 30분 OFF→ON 원시 자료:
  `tmp/target-192.168.214.4/rtsp_sei_resource_ab_30min_20260813/`
- 역순 10분 ON→OFF 원시 자료:
  `tmp/target-192.168.214.4/rtsp_sei_resource_ab_reverse_10min_20260813/`
- 120초 삽입시간 원시 자료:
  `tmp/target-192.168.214.4/rtsp_sei_insert_timing_20260813/`
