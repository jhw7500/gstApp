# 카메라 시간 동기화 검증 기록

## 1. 문서 목적

이 문서는 4채널 카메라 시간 동기화 검증의 누적 기준 문서다. 앞으로
진행하는 모든 시험은 이 문서에 시험 목적, 환경, 설정, 절차, 원시 로그 위치,
결과, 해석 및 판정 한계를 추가한다.

> **2026-08-13 설정 전환 주의:** 아래 시험 이력의 `GSTAPP_*` 명령과 환경변수는
> 당시 측정 입력을 정확히 보존한 **과거 증거**다. 현재 바이너리의 동기화 설정은
> JSON/CLI로 전달하며, 과거 측정값이나 당시 실행 조건을 새 인터페이스로 소급해
> 고쳐 쓰지 않는다.

검증은 영상 경로의 앞단부터 다음 순서로 진행한다.

1. 센서 FSYNC와 센서 프레임 생성
2. AP1302 센서 입력과 ISP 출력
3. MAX9296, MIPI CSI2, ISI, V4L2 입력
4. 채널별 crop, queue, encoder 경로
5. 녹화/RTSP의 `appsink`-`appsrc` 전달 및 타임스탬프 정책

최종적으로 확인할 사항은 다음과 같다.

- ch0/ch1 및 ch2/ch3가 동일한 전송 프레임을 누락 없이 받는가?
- CSI0과 CSI1이 동일 주파수로 동작하며 1프레임 오프셋이 없는가?
- GStreamer의 branch, queue, encoder 또는 app bridge에서 프레임 누락이나
  타임스탬프 차이가 추가되는가?
- 현재 증거가 전송 동기화만 증명하는지, 센서 노출 위상까지 증명하는지를
  명확하게 구분할 수 있는가?

## 2. 시험 환경

### 2.1 하드웨어 및 채널 구성

| 항목 | 값 |
| --- | --- |
| 타겟 | `pim-camera-v016` (`192.168.214.4`) |
| 센서 | AR0234 4개, FHD |
| ISP | AP1302 4개 |
| Deserializer | I2C bus 2 및 bus 1의 MAX9296, 주소 `0x48` |
| CSI0 구성 | bus 2, AP1302 `0x11`=ch0, `0x12`=ch1 |
| CSI1 구성 | bus 1, AP1302 `0x11`=ch2, `0x12`=ch3 |
| 물리 동기 신호 | CSI0/CSI1의 센서와 ISP가 동일 FSYNC pin 사용 |
| V4L2 장치 | CSI0(bus 2)=`/dev/video4`, CSI1(bus 1)=`/dev/video3` |
| V4L2 프레임 구성 | CSI별 FHD 2채널을 3840x1080 한 프레임으로 결합 |

공통 FSYNC 배선은 하드웨어 구성 사실이다. 아래 시험은 센서 노출 시작 신호를
직접 측정하지 않으므로, 공통 FSYNC 배선만으로 네 센서가 완전히 동일한 물리
프레임을 노출한다고 단정하지 않는다.

### 2.2 소프트웨어 및 실행 설정

| 항목 | 값 |
| --- | --- |
| 측정일 | 2026-08-12 |
| 타겟 GStreamer | 1.18.0 |
| 커널/SDK 계열 | i.MX8, Linux 5.10 Hardknott SDK |
| gstApp 실행 명령 | `gstApp -d 22 -m 4` |
| GStreamer 구성 | 최상위 pipeline 하나에 `v4l2src` bin 2개 포함 |
| 활성 채널 | ch0, ch1, ch2, ch3 |
| 해상도/프레임률 | 4채널 FHD, 15fps |
| V4L2 I/O mode | 4 (`dmabuf`) |
| 녹화/RTSP | 둘 다 활성, single encoder mode (`dual_enc=0`) |
| videorate | 활성(기본값) |
| overlay/audio/capture | 모두 비활성 |
| Encoder 입력 queue | 300ms, 3프레임 요청, 전체 256MiB 예산 |
| 설정 파일 | `/root/shared_v/edgeconf_pim.json` |

제품 사양은 4채널 FHD 15fps다. 사용자가 먼저 제공한 일부 로그는 임시 30fps
시험에서 생성됐지만, 이 문서의 타겟 직접 측정은 별도 표기가 없으면 현재 설정인
15fps로 수행했다.

### 2.3 현재 동기화 설정 인터페이스

현재 설정 우선순위는 `기본값 → JSON → CLI`이며, 뒤의 값이 앞의 값을
덮어쓴다. 일곱 설정의 기본값은 모두 비활성이다. 제품 기능인 Frame ID SEI는
`VHL_CAM.rtsp_tune.frame_id_sei` JSON boolean 또는 CLI로 설정할 수 있고,
나머지 trace/stall 설정은 CLI 전용이다.

| 과거 측정 입력(기록 보존용) | 현재 CLI | 용도 |
| --- | --- | --- |
| `GSTAPP_RTSP_FRAME_ID_SEI` | `--rtsp-frame-id-sei=0|1` | H.265 Frame ID SEI |
| `GSTAPP_V4L2_SYNC_TRACE_SEC` | `--v4l2-sync-trace-sec=N` | V4L2 동기화 trace |
| `GSTAPP_CHANNEL_SYNC_TRACE_SEC` | `--channel-sync-trace-sec=N` | 채널 경로 trace |
| `GSTAPP_RTSP_SYNC_TRACE_SEC` | `--rtsp-sync-trace-sec=N` | RTSP bridge/media trace |
| `GSTAPP_RTSP_TEST_STALL_CH` | `--rtsp-test-stall-ch=N` | 시험 정체 채널 |
| `GSTAPP_RTSP_TEST_STALL_AFTER_SEC` | `--rtsp-test-stall-after-sec=N` | 시험 정체 시작 시각 |
| `GSTAPP_RTSP_TEST_STALL_DURATION_SEC` | `--rtsp-test-stall-duration-sec=N` | 시험 정체 시간 |

현재 수동 계측은 운영 바이너리를 교체하지 않는다. service를 중지하고 기존
`gstApp`/`killcam` 종료를 확인한 뒤 `/root`에서 `/tmp` 시험 바이너리를 직접
실행한다. 예를 들면 다음과 같다.

```bash
cd /root
nohup /tmp/gstApp.sync-test -d 22 -m 4 \
  --rtsp-sync-trace-sec=60 \
  --rtsp-frame-id-sei=1 \
  >/tmp/gstApp.sync-test.log 2>&1 &
```

정체 주입 세 옵션은 `--test=1` 실행에서만 유효하다. 일반 제품 실행에는
사용하지 않는다.

## 3. 시험 및 결과

### 3.1 AP1302 DMA를 통한 AR0234 `FRAME_COUNT` (`0x303A`) 측정

#### 목적

GStreamer 타임스탬프를 보기 전에 네 센서가 동일한 프레임 수를 생성하는지
센서 레지스터로 확인한다.

#### 방법 및 설정

- AP1302 센서 DMA/SIPM을 통해 AR0234 `0x303A`를 읽었다.
- `cam_sensor_frame_sync.sh`가 작은 역방향 변화를 무조건 16비트 wrap으로
  처리하지 않도록 수정했다.
- 경과 시간과 `MAX_SENSOR_FPS`로 가능한 증가량을 제한하고, 불가능한 변화는
  `RESET_OR_INVALID`로 분류했다.
- 타겟에서 ch0/ch1과 ch2/ch3를 동시에 각각 30회 측정했다.
- AP1302 `SIPM_ERR_0/1`, AR0234 chip ID `0x3000`, 제어 레지스터
  `0x301A`를 함께 확인했다.

#### 결과

- 15fps 직접 측정에서 `0x303A` 값은 모두 `0x0000` 또는 `0xffff`였다.
- 수정된 스크립트는 이를 거대한 16비트 wrap으로 잘못 계산하지 않고
  `RESET_OR_INVALID`로 판정했다.
- 네 경로의 AP1302 `SIPM_ERR_0/1`은 모두 `0x0000`이었다.
- AR0234 `0x3000`은 모두 `0x0a56`, `0x301A`는 `0x2058` 또는
  `0x205c`로 정상 범위 값을 반환했다.
- 커널 로그상 DMA 동작은 성공했고 새로운 전송 오류는 없었으며, 읽은 값 자체가
  `0`/`ffff`였다.

#### 판정 및 한계

센서/AP1302 제어 경로는 동작하지만, 현재 DMA 경로에서 `0x303A`를 신뢰할 수
있는 프레임 카운터로 사용할 수 없거나 레지스터 의미가 예상과 다르다. 따라서
이 레지스터만으로 동기화 여부를 판정하지 않는다.

#### 원시 로그

- `tmp/target-192.168.214.4/ch01_sync_v2.log`
- `tmp/target-192.168.214.4/ch23_sync_v2.log`
- `tmp/target-192.168.214.4/frame_sync_dmesg_before.log`
- `tmp/target-192.168.214.4/frame_sync_dmesg_after.log`
- `tmp/target-192.168.214.4/frame_sync_diag_after.log`

이전 30fps 로그는 잘못된 modular-wrap 합계를 포함하므로 과거 자료로만 보존한다.

- `tmp/ch01_sync.log`
- `tmp/ch23_sync.log`
- `tmp/ch0123_sync.log`

### 3.2 AP1302 HINF 출력 프레임 카운터 측정

#### 목적

네 AP1302 출력이 같은 프레임률로 동작하며 출력 프레임 누락이 없는지 확인한다.

#### 방법 및 설정

- AP1302 `FRAME_CNT` 레지스터 `0x0002`를 읽었다.
- 상위 byte를 HINF 출력 카운터, 하위 byte를 BRAC로 해석했다.
- 네 AP1302를 14.799906211초 동안 반복 측정했다.

#### 결과

- 네 HINF 카운터가 모두 정확히 222프레임 증가했다.
- 네 장치의 계산 프레임률은 약 15.0001fps였다.
- 동일 CSI 내 두 AP1302의 순차 읽기 차이는 0 또는 1카운트였다.
- CSI 그룹 간 절대 카운터에는 약 18카운트의 고정 초기화 차이가 있었다.

#### 판정 및 한계

측정 구간 동안 네 AP1302 출력은 프레임률과 증가량이 같았고 출력 누락은
관찰되지 않았다. 카운터의 절대값은 초기화 시점의 영향을 받으므로 동일한 물리
프레임 번호를 뜻하지 않는다. 순차 I2C 읽기로 노출 위상을 증명할 수도 없다.

#### 원시 로그

- `tmp/target-192.168.214.4/ap1302_hinf_sync.log`

### 3.3 AP1302 입력 FV 카운터와 HINF 출력 카운터 비교

#### 목적

신뢰할 수 없었던 센서 `0x303A` 대신 AP1302의 센서 입력 Frame Valid 카운터와
출력 카운터를 비교하여 센서 입력부터 ISP 출력까지의 프레임 누락을 확인한다.

#### 방법 및 설정

- AP1302 `0xf038`을 통해 advanced page `0x00490000`을 선택했다.
- 선택된 window의 `0xe040`, 즉 advanced 주소 `0x00490040`의
  `AP1302_ADV_CAPTURE_A_FV_CNT`를 읽었다.
- 매 측정마다 기존 advanced page 값을 저장하고 읽기 후 복원했다.
- 네 AP1302의 입력 FV와 HINF를 순서대로 10회 읽었다.
- 측정 시간은 18.867396116초였다.

#### 결과

| AP1302 | 입력 FV 증가 | HINF 증가 | 차이 |
| --- | ---: | ---: | ---: |
| bus2 `0x11` / ch0 | 283 | 282 | +1 |
| bus2 `0x12` / ch1 | 283 | 282 | +1 |
| bus1 `0x11` / ch2 | 282 | 283 | -1 |
| bus1 `0x12` / ch3 | 282 | 282 | 0 |

각 AP1302에서 입력 FV와 출력 HINF의 low byte 관계는 전체 측정 동안 1카운트
범위 안에서 유지됐다. +/-1 차이는 두 레지스터를 순서대로 읽는 사이 프레임
경계를 통과한 결과와 일치한다.

#### 판정 및 한계

센서 입력부터 AP1302 출력 사이의 지속적인 프레임 누락은 관찰되지 않았다.
이는 프레임 수와 프레임률에 대한 결과이며 센서 노출 위상 증거는 아니다.

#### 원시 로그

- `tmp/target-192.168.214.4/ap1302_icp_hinf_sync.log`

### 3.4 CSI 및 ISI interrupt 증가량 측정

#### 목적

gstApp이 V4L2 장치를 사용 중인 상태에서 두 전송 경로의 이벤트가 같은 속도로
진행하는지 확인한다.

#### 방법 및 설정

gstApp을 중지하지 않고 10.006초 전후의 `/proc/interrupts` 차이를 계산했다.

#### 결과

| 장치 | IRQ 증가량 |
| --- | ---: |
| `32e40000.csi` / CSI0 | 302 |
| `32e50000.csi` / CSI1 | 302 |
| `32e00000.isi` | 180 |
| `32e02000.isi` | 186 |

#### 판정 및 한계

두 CSI 이벤트 수는 정확히 같았다. ISI interrupt 수는 직접적인 프레임 sequence
카운터가 아니므로 ISI 차이 6을 프레임 6개 불일치로 해석하지 않는다.

#### 원시 로그

- `tmp/target-192.168.214.4/csi_isi_irq_10s.log`

### 3.5 `v4l2src`의 V4L2 sequence 및 PTS 측정

#### 목적

애플리케이션 입력 경계에서 프레임 연속성을 확인하고 CSI0과 CSI1의 시간차를
수치화한다.

#### 방법 및 설정

- 각 `v4l2src` 바로 뒤 source pad에 환경변수 기반 probe를 추가했다.
- 활성화 환경변수는 `GSTAPP_V4L2_SYNC_TRACE_SEC=60`이다.
- `GST_BUFFER_OFFSET`의 V4L2 sequence, PTS, monotonic callback 시각,
  sequence 차이 및 PTS 차이를 기록했다.
- `./make-for-imx8`로 빌드한 임시 계측 바이너리를 타겟에 배포했다.
- 운영 바이너리를 먼저 백업하고 측정 종료 후 원래 바이너리와 환경으로 복구했다.
- 시작 시 PTS가 0인 버퍼 3개는 제외했다. 이 구간에서 GStreamer sequence가
  `0,3,4`에서 `252`로 재설정되므로 이를 누락으로 계산하면 잘못된
  `lost=249`가 된다.

#### 결과

| 항목 | CSI0 | CSI1 |
| --- | ---: | ---: |
| 유효 프레임 | 897 | 897 |
| Sequence 범위 | 252..1148 | 252..1148 |
| Sequence 누락 | 0 | 0 |
| PTS 역행 | 0 | 0 |
| 계산 프레임률 | 14.98555fps | 14.98561fps |

sequence가 같은 CSI0/CSI1 프레임 897쌍의 차이는 다음과 같다.

| CSI0-CSI1 차이 | 결과 |
| --- | ---: |
| 평균 signed PTS 차이 | +0.005825ms |
| 절댓값 중앙값 | 0.151127ms |
| 절댓값 P95 | 0.456534ms |
| 절댓값 P99 | 0.723798ms |
| 절댓값 최대 | 2.751550ms |
| 1ms 이내 프레임 | 894 / 897 |
| Sequence 집합 완전 일치 | true |

monotonic callback 도착 차이도 PTS와 유사하게 절댓값 중앙값 0.145ms,
P95 0.474ms, 최대 2.753ms였다.

#### 판정 및 한계

- 안정 구간 60초 동안 V4L2 sequence 누락은 없었다.
- CSI0과 CSI1 사이에서 15fps의 1프레임에 해당하는 약 66.7ms 오프셋은
  관찰되지 않았다.
- 각 CSI는 3840x1080 결합 프레임이므로 crop 전에는 ch0/ch1이 동일한
  V4L2 sequence/PTS를 공유하고 ch2/ch3도 동일한 값을 공유한다.
- V4L2 PTS와 callback 시각은 전송/반출 시각이며 센서 노출 시작 시각은 아니다.
  따라서 전송 동기화의 강한 증거지만 물리 노출 위상을 직접 증명하지 않는다.

#### 코드 및 원시 로그

- 구현: `videoBin.cpp`
- 활성 환경변수가 없으면 probe를 설치하지 않는다.
- 최종 코드는 PTS=0인 시작 버퍼를 warm-up으로 제외한다.
- `tmp/target-192.168.214.4/v4l2_sync_trace.log`
- `tmp/target-192.168.214.4/v4l2_sync_analysis.txt`
- `tmp/target-192.168.214.4/v4l2_sync_measurement_status.log`
- `tmp/target-192.168.214.4/v4l2_sync_restore_status.log`

### 3.6 채널 branch, crop 및 encoder 입력 비교

#### 목적

V4L2 결합 프레임이 ch0~ch3으로 분기된 후 crop, 변환, videorate를 거쳐
encoder 입력에 도달할 때 채널별 프레임 누락이나 PTS 차이가 생기는지 확인한다.

#### 실제 pipeline 계측 위치

현재 `dual_enc=0` 경로는 다음 순서다.

```text
v4l2src -> ... -> teeCrop
                  -> encoder queue(branch_in)
                  -> videocrop(crop_out)
                  -> imxvideoconvert_g2d
                  -> videorate
                  -> capsfilter
                  -> encoder sink(enc_in)
```

#### 방법 및 설정

- `encoderBin.cpp`에 환경변수 기반 probe를 추가했다.
- 활성화 환경변수는 `GSTAPP_CHANNEL_SYNC_TRACE_SEC=60`이다.
- ch0~ch3 각각에서 `branch_in`, `crop_out`, `enc_in` 세 지점을 측정했다.
- PTS=0인 시작 버퍼는 warm-up으로 제외했다.
- 모든 지점에서 프레임 수, sequence 범위/누락, PTS 역행을 집계했다.
- 로그 부하를 줄이기 위해 `enc_in`만 프레임별 sequence/PTS를 기록하고,
  `branch_in`과 `crop_out`은 60초 요약만 기록했다.
- 임시 바이너리 checksum은
  `653f30d2f3b156cb60fd8dd6eedda62ee7ce8240ca62646bd52c61bb1a245496`였다.
- 측정 후 운영 바이너리 checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구하고 계측 환경변수를 제거했다.

#### 결과: branch 및 crop

| 채널 | 지점 | 프레임 | Sequence | 누락 | reset | PTS 역행 |
| --- | --- | ---: | --- | ---: | ---: | ---: |
| ch0 | branch_in | 900 | 263..1162 | 0 | 0 | 0 |
| ch0 | crop_out | 900 | 263..1162 | 0 | 0 | 0 |
| ch1 | branch_in | 900 | 263..1162 | 0 | 0 | 0 |
| ch1 | crop_out | 900 | 263..1162 | 0 | 0 | 0 |
| ch2 | branch_in | 900 | 263..1162 | 0 | 0 | 0 |
| ch2 | crop_out | 900 | 263..1162 | 0 | 0 | 0 |
| ch3 | branch_in | 900 | 263..1162 | 0 | 0 | 0 |
| ch3 | crop_out | 900 | 263..1162 | 0 | 0 | 0 |

네 채널 모두 branch 입력과 crop 출력의 프레임 수 및 sequence 범위가 정확히
같았다. crop 전후의 누락, sequence reset, PTS 역행은 없었다.

#### 결과: encoder 입력

| 채널 | 프레임 | Sequence | PTS 범위 | 누락 | PTS 역행 |
| --- | ---: | --- | --- | ---: | ---: |
| ch0 | 900 | 1..900 | 66,666,666..60,000,000,000ns | 0 | 0 |
| ch1 | 900 | 1..900 | 66,666,666..60,000,000,000ns | 0 | 0 |
| ch2 | 900 | 1..900 | 66,666,666..60,000,000,000ns | 0 | 0 |
| ch3 | 900 | 1..900 | 66,666,666..60,000,000,000ns | 0 | 0 |

- 900개 모든 sequence에서 네 채널 PTS가 bit 단위로 완전히 같았다.
- PTS 간격은 15fps 기준의 66,666,666ns 또는 66,666,667ns였다.
- `enc_in`의 sequence와 PTS는 활성화된 `videorate`를 지난 뒤 1부터 시작하는
  이상적인 15fps timeline으로 재생성됐다.
- 네 streaming thread의 callback 실행 시각 spread는 중앙값 9.7765ms,
  P95 17.5758ms, 최대 84.951ms였다. 이는 PTS 차이가 아니라 CPU/VPU 처리 및
  thread scheduling 순서다.

#### 판정 및 한계

- `branch_in`에서 `crop_out`까지 채널별 프레임 누락은 없었다.
- `crop_out`과 `enc_in`의 프레임 수 역시 모두 900으로 같아 순 프레임 수 손실은
  관찰되지 않았다.
- encoder 입력 PTS는 네 채널이 완전히 같은 15fps timeline이다.
- 다만 `videorate`가 sequence와 PTS를 다시 생성하므로, `enc_in`의 동일 PTS만으로
  입력의 동일 물리 프레임임을 증명하지 않는다. 현재는 입력 900개와 출력 900개가
  동일하다는 순 프레임 수 증거다. drop 1개와 duplicate 1개가 동시에 발생하는
  특수 상황까지 배제하려면 `videorate` sink/src를 같은 실행에서 프레임별로
  대응시켜야 한다.
- callback 실행 시각 차이는 병렬 처리 scheduling 지연이므로 프레임 동기화
  판정 기준으로 사용하지 않는다.

#### 코드 및 원시 로그

- 구현: `encoderBin.cpp`
- 활성 환경변수가 없으면 probe를 설치하지 않는다.
- `tmp/target-192.168.214.4/channel_sync_trace.log`
- `tmp/target-192.168.214.4/channel_sync_analysis.txt`
- `tmp/target-192.168.214.4/channel_sync_measurement_status.log`
- `tmp/target-192.168.214.4/channel_sync_restore_status.log`

### 3.7 videorate, encoder 출력 및 record/RTSP 분기 비교

#### 목적

3.6에서 남은 다음 세 가지를 같은 60초 구간에서 구분해 확인한다.

1. `videorate`가 실제로 drop 또는 duplicate를 수행하는지
2. encoder 입력 프레임과 압축 출력 access unit 수가 같은지
3. encoder 출력 tee에서 record와 RTSP 분기로 나뉜 뒤 손실이나 PTS 변화가 있는지

#### 실제 pipeline 계측 위치

```text
branch_in -> crop_out -> rate_in -> videorate -> rate_out
           -> enc_in -> encoder -> enc_out -> tee
                                      |-> record_branch
                                      `-> rtsp_branch
```

`record_branch`와 `rtsp_branch`는 encoder 출력 tee의 각 source pad이며,
RTSP `appsink`-`appsrc` bridge보다 앞쪽이다.

#### 방법 및 설정

- 타겟: `root@192.168.214.4`
- 동작 설정: 4채널 FHD, 채널당 15fps 출력 pipeline
- `GSTAPP_CHANNEL_SYNC_TRACE_SEC=60`으로 ch0~ch3의 8개 지점을 동시에 계측했다.
- 각 지점에서 버퍼 수, PTS 범위, PTS 역행, sequence 유효성 및 압축
  keyframe 수를 요약했다.
- 순서가 포함된 PTS 목록의 FNV-1a 64-bit hash를 계산해, 단순히 첫/마지막
  PTS만 같은 경우가 아니라 900개 PTS 전체가 같은지 비교했다.
- `videorate`의 `in`, `out`, `drop`, `duplicate` 누적 property를 측정 시작값과
  종료값의 차이로 기록했다.
- 시작 시 PTS=0인 warm-up 버퍼는 제외했다. `rate_in` 계열에서는 3개,
  `rate_out` 이후에서는 1개가 제외됐다.
- 임시 계측 바이너리 checksum은
  `9c1d1182fb90b66c251c4656373f2ca3aaf1fda4e9b47558b19d89966ff8a75c`였다.
- 측정 후 운영 바이너리 checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구하고 계측 환경변수를 제거했다.

#### 결과: videorate

| 채널 | 입력 | 출력 | drop | duplicate | 출력 Sequence | 출력 PTS 범위 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| ch0 | 901 | 900 | 1 | 0 | 1..900 | 66,666,666..60,000,000,000ns |
| ch1 | 901 | 900 | 1 | 0 | 1..900 | 66,666,666..60,000,000,000ns |
| ch2 | 901 | 900 | 1 | 0 | 1..900 | 66,666,666..60,000,000,000ns |
| ch3 | 901 | 900 | 1 | 0 | 1..900 | 66,666,666..60,000,000,000ns |

- 네 `videorate` 모두 측정 property 차이가 `in=901`, `out=900`, `drop=1`,
  `duplicate=0`으로 같았다.
- `rate_out`의 900개 PTS hash는 네 채널 모두
  `131d6c0a5632c431`로 완전히 같았다.
- `rate_in`은 ch0/ch1이 동일한 hash `6bc921df80699965`, ch2/ch3이 동일한
  hash `a2ede253c5780880`였다. 두 CSI 사이 hash가 다른 것은 원래 V4L2 PTS의
  sub-ms 차이가 그대로 포함되기 때문이며, 3.5의 측정 결과와 일치한다.

#### 결과: encoder와 출력 분기

아래 결과는 ch0~ch3 네 채널에서 모두 같았다.

| 지점 | 버퍼/AU 수 | PTS 범위 | PTS hash | PTS 역행 | 압축 keyframe |
| --- | ---: | --- | --- | ---: | ---: |
| `rate_out` | 900 | 66,666,666..60,000,000,000ns | `131d6c0a5632c431` | 0 | 해당 없음 |
| `enc_in` | 900 | 66,666,666..60,000,000,000ns | `131d6c0a5632c431` | 0 | 해당 없음 |
| `enc_out` | 900 | 66,666,666..60,000,000,000ns | `131d6c0a5632c431` | 0 | 31 |
| `record_branch` | 900 | 66,666,666..60,000,000,000ns | `131d6c0a5632c431` | 0 | 31 |
| `rtsp_branch` | 900 | 66,666,666..60,000,000,000ns | `131d6c0a5632c431` | 0 | 31 |

- encoder 입력 900개와 압축 출력 access unit 900개가 정확히 대응했다.
- encoder 출력 이후 record/RTSP 양쪽 분기에서 버퍼 수와 전체 PTS 순서가
  바뀌지 않았다.
- 압축 출력에서는 V4L2 sequence가 `GstBuffer::offset`에 전달되지 않아
  `invalid_seq=900`으로 집계된다. 이는 프레임 누락을 의미하지 않는다.
- raw buffer는 `DELTA_UNIT` flag가 없으므로 계측상 모두 keyframe처럼 보인다.
  유효하게 해석할 수 있는 값은 압축 출력과 그 분기의 채널당 31개뿐이다.

#### RTSP timestamp 재기록 코드 확인

이번 동적 계측은 `rtsp_branch`, 즉 RTSP bridge 입력 전까지의 기존 PTS가
보존됨을 확인했다. 그 다음 `rtspServerBin.cpp`의 `new_sample_handler()`는
`gst_buffer_copy_region(..., GST_BUFFER_COPY_MEMORY, ...)`만 사용해 memory는
공유하되 timestamp와 metadata를 의도적으로 복사하지 않는다. RTSP factory의
`appsrc`는 `do-timestamp=1`, `is-live=1`, `format=3`으로 생성되므로 별도 RTSP
media pipeline의 running time을 기준으로 새 timestamp가 붙는다.

코드 주석에 적힌 목적은 서로 다른 pipeline 사이의 clock/synchronization 문제를
피하는 것이다. 대신 센서/V4L2에서 유래한 upstream PTS와 RTSP 송출 PTS를 직접
대응시킬 수 없게 된다. 따라서 현재 결과는 bridge 이전까지만 증명하며, 실제
RTSP client가 받는 timestamp 동기화는 별도 계측이 필요하다.

#### 판정 및 한계

- 각 채널에서 `videorate`가 60초 계측 구간 동안 입력 하나를 drop했고
  duplicate는 하지 않았다.
- `videorate` 출력 이후 encoder와 두 tee 분기까지 추가 손실, PTS 역행 또는
  채널별 PTS 차이는 관찰되지 않았다.
- 네 채널의 `drop=1`이 같다는 사실만으로 네 `videorate`가 **동일한 물리 입력
  프레임**을 버렸다고 단정할 수 없다. 각 instance는 독립적으로 입력 PTS를
  판단하며 출력 PTS를 이상적인 15fps timeline으로 다시 생성한다.
- 따라서 네 채널 영상의 물리 프레임 동일성을 끝까지 추적하려면 `rate_in`의
  원본 sequence 또는 buffer lineage를 `rate_out`까지 대응시켜 실제로 제외된
  입력 sequence를 비교해야 한다.

#### 코드 및 원시 로그

- 구현: `encoderBin.cpp`
- 활성 환경변수가 없으면 probe를 설치하지 않는다.
- `tmp/target-192.168.214.4/post_sync_trace.log`
- `tmp/target-192.168.214.4/post_sync_analysis.txt`
- `tmp/target-192.168.214.4/post_sync_measurement_status.log`
- `tmp/target-192.168.214.4/post_sync_restore_status.log`

### 3.8 videorate 원본 프레임 lineage 비교

#### 목적

3.7에서 네 채널의 출력 PTS가 같다는 것은 확인했지만, `videorate`가 PTS와
offset을 다시 생성하므로 동일한 물리 입력 프레임을 선택했다는 사실까지는
증명하지 못했다. 이번 시험에서는 `rate_in`의 원본 V4L2 sequence를
`rate_out`까지 전달해, 네 채널이 제외하거나 중복한 원본 sequence와 전체 선택
순서를 직접 비교한다.

#### 방법 및 설정

- 타겟: `root@192.168.214.4`
- 동작 설정: 4채널 FHD, 채널당 15fps 출력 pipeline
- 계측 시간: 60초
- 환경변수: `GSTAPP_CHANNEL_SYNC_TRACE_SEC=60`
- 최종 보강 계측 바이너리 checksum:
  `5c688a5bb95eabc6de39570fe3c87ffed29acfbbae55ff100d829748730d92f1`
- `rate_in` buffer에 채널별 `GstReferenceTimestampMeta`를 추가했다.
  - `timestamp`: 원본 V4L2 sequence
  - `duration`: 원본 입력 PTS
- 타겟 SDK의 GStreamer 1.18 `gstvideorate.c`와 `gstbuffer.c`를 확인한 결과,
  `videorate`가 writable output buffer를 만들 때
  `GstReferenceTimestampMeta`의 transform 함수가 meta를 복사한다.
- 따라서 영상 memory, 기존 PTS 또는 encoder 입력 데이터는 변경하지 않고
  원본 sequence와 PTS만 측정용 meta로 추적했다.
- 출력 원본 sequence 전체에 순서 의존 FNV-1a 64-bit hash를 계산했다.
- `videorate`의 한 프레임 look-ahead 때문에 아직 결정되지 않은 마지막 입력은
  drop 판정에서 제외했다.

#### 결과: 안정 입력 구간

| 채널 | `rate_in` 프레임 | Sequence | 누락 | PTS hash |
| --- | ---: | --- | ---: | --- |
| ch0 | 900 | 263..1162 | 0 | `0e5557319bf3aacb` |
| ch1 | 900 | 263..1162 | 0 | `0e5557319bf3aacb` |
| ch2 | 900 | 263..1162 | 0 | `6509e91c5dc7da12` |
| ch3 | 900 | 263..1162 | 0 | `6509e91c5dc7da12` |

- 안정 구간의 V4L2 sequence 범위는 네 채널 모두 같고 연속적이었다.
- 같은 CSI에서 crop으로 나뉜 ch0/ch1, ch2/ch3은 각각 원본 PTS 목록도 완전히
  같았다.
- CSI0/CSI1 사이 PTS hash가 다른 것은 3.5에서 확인한 sub-ms 수준의 V4L2 PTS
  차이가 각 항목에 포함되기 때문이다.

#### 결과: 원본 프레임 선택

| 채널 | lineage 입력 | 출력 | 제외 원본 Sequence | 중복 원본 Sequence | 원본 Sequence hash |
| --- | ---: | ---: | --- | --- | --- |
| ch0 | 903 | 900 | 0, 4, 490 | 489, 493 | `38faed575a2e3d3d` |
| ch1 | 903 | 900 | 0, 4, 490 | 489, 493 | `38faed575a2e3d3d` |
| ch2 | 903 | 900 | 0, 4, 490 | 489, 493 | `38faed575a2e3d3d` |
| ch3 | 903 | 900 | 0, 4, 490 | 489, 493 | `38faed575a2e3d3d` |

- sequence 0과 4는 PTS 기준이 잡히기 전의 startup/warm-up 입력이다.
- 안정 구간에서는 네 채널 모두 sequence 490을 제외했고 sequence 489와 493을
  각각 한 번 더 출력했다.
- 원본 sequence 900개의 **순서 hash도 네 채널이 완전히 같았다.**
- 원본 sequence 역행, 출력 meta 누락 및 meta 추가 실패는 모두 0이었다.
- 같은 실행의 `videorate` property 변화도 네 채널 모두
  `in=900`, `out=900`, `drop=2`, `duplicate=2`로 같았다.
- 3.7 실행의 `in=901`, `out=900`, `drop=1`, `duplicate=0`과 값이 다른 것은
  pipeline 기동 위상과 입력 PTS jitter에 따라 `videorate`의 선택이 실행별로
  달라질 수 있음을 보여준다. 중요한 판정 기준은 같은 실행에서 네 채널이 동일한
  원본 sequence를 선택했는지 여부다.

선택된 마지막 원본 PTS는 ch0/ch1이 `60,015,245,579ns`, ch2/ch3이
`60,015,245,704ns`로 CSI 간 차이는 125ns였다. 전체 원본 PTS hash는 같은
CSI pair 내부에서 일치했으며, 서로 다른 CSI 사이에서는 원래의 미세 PTS 차이로
인해 달랐다.

#### 판정 및 한계

- 이번 실행에서는 네 독립 `videorate` instance가 drop과 duplicate를
  수행했지만, 제외 sequence, 중복 sequence 및 전체 출력 원본 sequence 순서가
  모두 정확히 같았다.
- 따라서 `videorate` 이후 네 채널의 동일 PTS는 단순한 timestamp 재생성만의
  결과가 아니라 **동일한 원본 V4L2 sequence를 선택한 결과**임을 확인했다.
- 기존 V4L2 PTS 비교 결과와 결합하면 애플리케이션의 encoder 입력까지
  1프레임인 약 66.7ms 어긋남은 관찰되지 않았다.
- V4L2 sequence와 PTS는 센서 노출 시작을 직접 표시하지 않는다. 네 센서의
  물리 노출 위상이 sub-ms 범위에서 같은지에 대한 한계는 그대로 남는다.

#### 코드, 원시 로그 및 원복

- 구현: `encoderBin.cpp`
- 환경변수가 없으면 lineage meta와 probe를 설치하지 않는다.
- 최종 계측:
  - `tmp/target-192.168.214.4/rate_lineage_trace.log`
  - `tmp/target-192.168.214.4/rate_lineage_analysis.txt`
  - `tmp/target-192.168.214.4/rate_lineage_measurement_status.log`
  - `tmp/target-192.168.214.4/rate_lineage_restore_status.log`
- 1차 보조 계측은 최종 hash 항목을 추가하기 전 결과이므로
  `rate_lineage_v1_*` 파일로 보존했다.
- 측정 후 운영 바이너리 checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다.
- `cam-operate.service=active`, 계측 환경변수 0개, video FD 4개 및 녹화 시작
  marker 생성을 확인했다.

### 3.9 RTSP `appsink`-`appsrc` bridge와 RTP 출력 비교

#### 목적

3.7까지 보존되던 encoder 출력 PTS가 RTSP bridge에서 의도적으로 제거된 뒤,
별도 RTSP media pipeline에서 다음 사항을 확인한다.

1. `appsink` 입력 access unit이 `appsrc`로 누락 없이 전달되는가?
2. 기존 PTS가 실제로 제거되고 `do-timestamp`의 새 PTS가 붙는가?
3. `appsrc` 출력부터 payloader 입력 및 RTP 출력까지 프레임 수가 유지되는가?
4. 네 RTSP mount를 동시에 연결했을 때 같은 원본 프레임에서 시작하며 서로
   비교할 수 있는 공통 timestamp를 갖는가?

#### 방법 및 설정

- 타겟: `root@192.168.214.4`
- 동작 설정: 4채널 FHD 15fps, H.265, RTSP TCP
- mount: `rtsp://user:user@127.0.0.1:8554/ch0` ~ `/ch3`
- 계측 시간: 60초
- 환경변수: `GSTAPP_RTSP_SYNC_TRACE_SEC=60`
- RTSP client는 네 mount에 병렬 연결하고 각각 100초 동안
  `rtspsrc latency=100 ! rtph265depay ! h265parse ! fakesink`로 수신했다.
- 최종 계측 바이너리 checksum:
  `ace13e21b5ca592de3cc715f7a2426651f5e6c4c898ed9127661ee9eff26a6e0`
- `new_sample_handler()`에서 다음 값을 집계했다.
  - 기존 upstream PTS 및 순서 hash
  - appsrc 부재, queue full, keyframe 대기 및 종료 중 drop 수
  - buffer copy, 측정 meta 추가 및 `gst_app_src_push_buffer()` 결과
- timestamp를 복사하지 않은 출력 buffer에 계측이 활성화된 경우에만
  `GstReferenceTimestampMeta`를 붙였다.
  - `timestamp`: bridge 입력의 기존 upstream PTS
  - `duration`: 측정 구간의 원본 순번
- RTSP media pipeline의 `appsrc` source, payloader sink 및 payloader source에
  probe를 설치했다. payloader source에서는 RTP 고정 header의 marker bit와
  90kHz timestamp를 읽었다.
- 이 계측은 환경변수가 없으면 probe와 meta를 추가하지 않는다.
- 시험 과정에서 발견한 계측 조건은 다음과 같이 수정한 뒤 최종 시험을 다시
  수행했다.
  - 앱 기동 직후 client 접속 대신 pipeline의 `async-done`과 녹화 시작 marker를
    기다린 후 연결했다.
  - H.265 payloader가 `GstBufferList`를 출력하므로 probe info type을 먼저
    확인한 후 list 안의 각 RTP packet을 읽도록 수정했다.

#### 결과: bridge 전달

| 채널 | 원본 첫 PTS | 원본 마지막 PTS | 계측 AU | push 성공 | 모든 drop/실패 | 제거 후 유효 PTS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ch0 | 3,266,666,666ns | 63,200,000,000ns | 900 | 900 | 0 | 0 |
| ch1 | 3,333,333,333ns | 63,266,666,666ns | 900 | 900 | 0 | 0 |
| ch2 | 3,400,000,000ns | 63,333,333,333ns | 900 | 900 | 0 | 0 |
| ch3 | 3,466,666,666ns | 63,400,000,000ns | 900 | 900 | 0 | 0 |

여기서 모든 drop/실패는 `no_appsrc`, `full_drop`, `key_wait_drop`,
`interrupted_drop`, `copy_fail`, `meta_fail`, `push_fail`의 합계이며, 각 항목은
모든 채널에서 각각 0이었다. `stripped_pts_valid=0`이므로 기존 PTS가 복사되지
않았고, 900개 push가 모두 성공했다.

네 RTSP media가 독립적으로 생성되면서 bridge 계측 시작 원본 PTS는 ch0부터
ch3까지 정확히 66,666,667ns씩 늦어졌다. 가장 빠른 ch0과 가장 늦은 ch3의
시작 차이는 200ms, 즉 15fps 기준 3프레임이었다. 이는 카메라 또는 encoder의
원본 프레임 위상이 3프레임 어긋났다는 뜻이 아니라, 각 mount의 appsrc가 준비된
시점부터 전달을 시작하기 때문에 이번 동시 접속에서 **client에 제공하기 시작한
원본 프레임**이 0, 1, 2, 3프레임 차이 났다는 의미다.

#### 결과: 새 PTS와 payloader

| 채널 | `appsrc_out` 프레임 | 첫 새 PTS | PTS 평균 간격 | 최소..최대 간격 | `pay_in` 일치 |
| --- | ---: | ---: | ---: | --- | --- |
| ch0 | 901 | 3,802,714ns | 66,638,080ns | 6,888,788..90,514,534ns | 완전 일치 |
| ch1 | 901 | 56,532,199ns | 66,653,768ns | 15,569,119..90,370,803ns | 완전 일치 |
| ch2 | 900 | 42,310,616ns | 66,666,228ns | 7,694,058..91,956,586ns | 완전 일치 |
| ch3 | 901 | 57,262,467ns | 66,645,113ns | 16,132,508..92,740,605ns | 완전 일치 |

- 모든 채널에서 새 PTS는 유효하고 역행하지 않았다.
- 각 채널의 `appsrc_out`과 `pay_in`은 프레임 수, 전체 PTS hash, 정규화 PTS
  hash 및 원본 lineage hash가 완전히 같았다. 두 지점 사이의 drop이나 PTS
  변경은 없었다.
- ch0, ch1, ch3의 901번째 항목에서 `missing_origin_meta=1`인 것은 bridge의
  60초 집계가 끝난 직후 자연스럽게 계속 전달된 경계 buffer다. 900개 계측
  원본의 PTS hash는 `appsrc_out`/`pay_in`의 원본 PTS hash와 각 채널에서
  정확히 같으므로 유실이나 중복을 뜻하지 않는다. ch2는 경계 도달 시점 차이로
  900개에서 요약됐다.
- 새 PTS 평균 간격은 15fps의 66.67ms와 일치하지만 개별 간격에는 약
  6.9~92.7ms의 scheduling jitter가 있었다. `do-timestamp`가 각 media
  pipeline의 running time을 사용하므로 네 채널의 첫 PTS와 전체 hash는 서로
  같지 않았다.

#### 결과: RTP와 실제 client 수신

| 채널 | RTP packet | marker frame | 서로 다른 RTP timestamp | 역행 | 평균 timestamp 간격 |
| --- | ---: | ---: | ---: | ---: | ---: |
| ch0 | 22,773 | 901 | 901 | 0 | 5,997 tick |
| ch1 | 22,745 | 901 | 901 | 0 | 5,998 tick |
| ch2 | 22,714 | 900 | 900 | 0 | 5,999 tick |
| ch3 | 11,675 | 901 | 901 | 0 | 5,998 tick |

- 모든 채널에서 RTP marker frame 수와 서로 다른 RTP timestamp 수가
  `appsrc_out` 프레임 수와 같았다. payloader 출력까지 프레임 누락은 없었다.
- 90kHz clock에서 15fps의 이상적인 간격은 6,000 tick이며, 측정 평균은
  5,997~5,999 tick이었다. 개별 최소/최대는 620~8,346 tick으로 새 PTS의
  scheduling jitter가 반영됐다.
- 최초 RTP timestamp는 ch0=`24,532,098`, ch1=`1,309,701,339`,
  ch2=`699,299,508`, ch3=`2,672,642,565`로 서로 무관한 시작값이었다.
  정규화 timestamp hash도 채널별 jitter 때문에 서로 달랐다.
- 100초 client 수신 결과는 ch0=1,487, ch1=1,486, ch2=1,485,
  ch3=1,484 access unit이었다. `timeout`에 의한 종료 code 124는 계획된
  종료이며, 네 client 모두 시험 시간 동안 지속적으로 수신했다.
- RTP packet 수 차이는 access unit 크기 및 압축량 차이다. marker frame 수가
  유지되므로 이를 프레임 손실로 해석하지 않는다.

#### 판정 및 한계

- RTSP bridge의 60초 측정 구간에서 appsrc 부재, queue full, keyframe 대기,
  copy 실패, push 실패에 의한 프레임 손실은 없었다.
- `appsrc` 출력부터 payloader 입력과 RTP marker까지 프레임 수가 유지됐다.
- 기존 upstream PTS 제거와 `do-timestamp=1`에 의한 새 PTS 부여가 코드 의도대로
  동작했다. 이 방식은 서로 다른 pipeline의 clock domain을 그대로 섞지 않는
  장점이 있지만, 원본 capture PTS와 RTSP PTS의 직접 대응 관계를 네트워크로
  전달하지 않는다.
- 각 RTSP media pipeline은 독립 running-time 기준과 독립 RTP timestamp
  시작값을 사용한다. 따라서 현재 client가 보는 PTS/RTP timestamp만으로는
  ch0~ch3의 동일 원본 프레임을 식별하거나 절대 시간을 서로 비교할 수 없다.
- 네 mount를 거의 동시에 연결해도 media 준비가 순차적으로 끝나므로, 이번
  시험에서는 제공 시작 원본 프레임이 최대 3프레임 달랐다. 현재 구조는 독립
  시청에는 문제가 없지만, 네 client가 첫 프레임부터 frame-aligned 상태로
  재생을 시작한다고 보장하지 않는다.
- 이 결과 역시 센서 노출 시작 시각을 직접 측정한 것은 아니다.

#### 코드, 원시 로그 및 원복

- 구현: `rtspServerBin.cpp`, `rtspServerBin.h`
- 최종 계측:
  - `tmp/target-192.168.214.4/rtsp_sync_trace.log`
  - `tmp/target-192.168.214.4/rtsp_sync_analysis.txt`
  - `tmp/target-192.168.214.4/rtsp_sync_measurement_status.log`
  - `tmp/target-192.168.214.4/rtsp_sync_restore_status.log`
  - `tmp/target-192.168.214.4/rtsp_sync_client_ch0.log` ~
    `rtsp_sync_client_ch3.log`
- 계측 조건을 보완하기 전 로그는 `rtsp_sync_trace_partial.log`,
  `rtsp_sync_trace_v2_partial.log` 등으로 보존했다.
- 최종 시험 후 운영 바이너리 checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다.
- `cam-operate.service=active`, 계측 환경변수 0개, video FD 4개 및 운영
  바이너리/process checksum 일치를 재확인했다. 타겟의 임시 계측 바이너리와
  실행 스크립트도 삭제했다.

### 3.10 RTCP Sender Report 기반 공통 시간축 검증

#### 목적

3.9에서 채널별 RTP timestamp 시작값은 서로 독립적임을 확인했다. 이번 시험은
RTCP Sender Report(SR)의 NTP↔RTP 매핑을 이용해 네 RTP stream을 공통 절대
시간축으로 변환하고, 같은 실행의 upstream 원본 PTS lineage와 결합해 동일 원본
프레임의 RTSP 송출 시간차를 계산한다.

#### 방법 및 설정

- 운영 설정은 3.9와 같은 4채널 FHD 15fps, H.265, RTSP/TCP다.
- 1차로 운영 바이너리를 변경하지 않고 네 client를 35초 동시 연결하고
  loopback RTSP/TCP interleaved packet을 capture했다.
  - RTCP SR 29개가 확인되어 기본 동작을 검증했다.
- 최종 시험은 `GSTAPP_RTSP_SYNC_TRACE_SEC=60` 계측과 RTCP capture를 같은
  실행에서 수행했다.
  - upstream 원본 PTS, 첫/마지막 RTP timestamp 및 RTCP SR 81개를 수집했다.
  - 네 client는 100초 동안 병렬 수신했다.
  - packet capture는 RTSP/RTCP header 해석에 필요한 앞 512byte만 저장해
    측정 부하와 저장 공간을 제한했다.
- 타겟 상태는 `System clock synchronized=yes`, `NTP service=active`였다.
- RTP timestamp `T`를 SR 기준 NTP로 다음과 같이 변환했다.

```text
frame_NTP = SR_NTP + signed32(T - SR_RTP) / 90000
origin_epoch = frame_NTP - upstream_origin_PTS
```

#### 결과: RTCP 매핑 안정성

| 채널 | TCP stream | SR 수 | SR phase 범위 | 표준편차 |
| --- | ---: | ---: | ---: | ---: |
| ch0 | 3 | 20 | 0.029861ms | 0.008210ms |
| ch1 | 2 | 20 | 0.028472ms | 0.008238ms |
| ch2 | 0 | 19 | 0.027778ms | 0.008212ms |
| ch3 | 1 | 22 | 0.021528ms | 0.005808ms |

- `phase = (SR_NTP * 90000 - SR_RTP) mod 6000`을 15fps frame 주기 안에서
  비교한 결과 각 채널의 RTCP 매핑은 전체 측정 동안 0.030ms 이내로
  안정적이었다.
- SR NTP와 loopback capture 시각 차이는 중앙값 0.398ms, P95 1.066ms였다.
- 따라서 시작값이 서로 다른 네 RTP timestamp를 SR을 통해 같은 NTP 축으로
  변환할 수 있다.

#### 결과: 동일 원본 PTS의 송출 시각

각 RTSP media가 제공을 시작한 첫 upstream 원본 PTS는 다음과 같았다.

| 채널 | 첫 원본 PTS | 첫 RTP timestamp | 같은 원본 PTS 보정 후 상대 NTP |
| --- | ---: | ---: | ---: |
| ch0 | 5.466666666s | 953,403,450 | +2.349377ms |
| ch1 | 5.400000000s | 2,631,215,171 | +4.376411ms |
| ch2 | 5.266666666s | 623,802,188 | +3.808975ms |
| ch3 | 5.333333333s | 1,897,920,757 | +0.000000ms |

- media 준비 순서로 첫 제공 원본 프레임은 ch2, ch3, ch1, ch0 순서로 한
  프레임씩 달랐고 전체 차이는 3프레임이었다.
- 첫 제공 frame 번호 차이를 원본 PTS로 보정하면, 동일 원본 프레임의 NTP 대응
  시각 spread는 측정 시작부 **4.376411ms**, 15fps 기준 **0.065646프레임**이었다.
- 약 60초 지점에서는 동일 원본 PTS의 NTP 대응 시각 spread가
  **18.223286ms**, **0.273349프레임**이었다.
- RTCP 매핑은 0.03ms 이내로 안정적이므로 위 변화의 주원인은 RTCP가 아니라
  각 media pipeline의 `do-timestamp`가 독립적인 buffer 처리 시각을 PTS로
  기록하면서 생긴 scheduling jitter다.
- 60초 동안 같은 원본 frame이 한 프레임인 66.667ms 이상 어긋나거나 순서가
  뒤바뀌는 현상은 관찰되지 않았다.

#### 판정 및 한계

- RTCP SR은 독립적인 RTP timestamp를 공통 NTP 시간축으로 바꾸는 데 사용할
  수 있다.
- 이번 정상 부하 60초 시험에서 동일 원본 프레임의 환산 송출 시간차는 최대
  18.223ms로 0.274프레임 미만이었다.
- 하지만 일반 RTSP client에는 계측에 사용한 upstream 원본 PTS/meta가 전달되지
  않는다. 따라서 client는 RTCP로 송출 시각을 맞출 수는 있어도 어느 RTP
  frame들이 동일 원본인지 자체적으로 확정할 수 없다.
- 동시 연결에서도 첫 제공 원본 frame이 최대 3개 달랐으므로, 공통 NTP만으로
  접속 이전 frame을 복구하거나 첫 frame부터 정렬할 수 없다.
- 상대 spread가 4.38ms에서 18.22ms로 변했으므로 장시간/고부하에서 반 프레임인
  33.3ms를 넘는지는 추가 장기 시험이 필요하다.

#### 원시 로그 및 원복

- 최종 결합 계측:
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined.pcapng`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_sr.csv`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_rtsp.csv`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_trace.log`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_analysis.txt`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_measurement_status.log`
  - `tmp/target-192.168.214.4/rtsp_rtcp_combined_restore_status.log`
- 1차 RTCP 확인 자료:
  - `tmp/target-192.168.214.4/rtsp_rtcp_sync.pcapng`
  - `tmp/target-192.168.214.4/rtsp_rtcp_sr.csv`
  - `tmp/target-192.168.214.4/rtsp_rtcp_sync_status.log`
- 최종 시험 후 운영 binary/process checksum을
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. 서비스 active, 계측 환경변수 0개, video FD 4개를 확인하고 타겟의
  임시 계측 파일과 스크립트를 삭제했다.

### 3.11 H.265 SEI 공통 Frame ID 전달 및 client 검증

#### 목적

3.10의 RTCP SR은 RTP timestamp를 공통 NTP 축으로 변환하지만, 일반 client는
어느 RTP frame들이 같은 원본인지 알 수 없다. 영상 내용을 비교하지 않고 동일
원본 프레임을 직접 식별하도록 encoder upstream PTS 기반 공통 frame ID를
H.265 SEI로 전달하고 client 수신 데이터에서 검증한다.

#### 전달 방식 선택

타겟 GStreamer 1.18 환경에서 다음 세 방식을 검토 및 시험했다.

1. **Custom RTP header extension:** RTP buffer API는 있지만 `rtph265pay`에서
   임의 확장을 SDP에 자동 협상하는 표면이 부족해 별도 송수신 구현과 일반
   client 호환성 부담이 크다.
2. **Upstream PTS clock rebase:** 원본 pipeline PTS를 RTSP media base time에
   맞춰 재기준화하는 환경변수 기반 prototype을 두 차례 실측했다.
   - clock 준비 전 buffer가 먼저 나간 1차 시험에서는 새 PTS로 전환될 때
     채널당 1회 역행했다.
   - clock 준비와 keyframe을 기다린 2차 시험에서는 역행은 없어졌지만 RTSP
     session 시작이 직렬화되어 시작 frame 차이가 커지고 일부 초기 RTP AU가
     누락됐다.
   - 두 시험 결과 모두 실제 적용에 부적합하므로 rebase 코드는 제거했다.
3. **H.265 registered user-data SEI:** 기존 RTP/RTCP timestamp와 decoder 동작을
   바꾸지 않으며, 표준 decoder는 사용하지 않는 supplemental data를 무시하고
   전용 client만 frame ID를 읽을 수 있어 최종 방식으로 선택했다.

#### SEI payload와 구현

- 환경변수: `GSTAPP_RTSP_FRAME_ID_SEI=1`
- H.265 prefix SEI registered user data payload는 28byte다.

| Offset | 크기 | 값 |
| ---: | ---: | --- |
| 0 | 8 | magic `GSTSYNC1` |
| 8 | 1 | version `1` |
| 9 | 1 | channel |
| 10 | 2 | reserved |
| 12 | 8 | big-endian `frame_id` |
| 20 | 8 | big-endian `origin_pts_ns` |

- `frame_id = round(origin_pts_ns * fps / GST_SECOND)`로 계산했다.
- 이번 15fps 경로에서는 3.8에서 확인한 공통 videorate PTS가 같은 frame ID를
  생성한다.
- `gst_h265_create_sei_memory()`와 `gst_h265_parser_insert_sei()`를 사용했다.
  따라서 `Makefile`에 `gstreamer-codecparsers-1.0`을 명시적으로 추가했다.
- SEI는 RTSP용 복사 buffer에만 추가되므로 record branch와 공유 encoder 출력은
  변경하지 않는다.
- timestamp 제거 및 `do-timestamp=1` 정책도 그대로 유지한다.
- 환경변수가 없으면 SEI parser를 생성하거나 bitstream을 변경하지 않는다.

#### 시험 환경

- 4채널 FHD 15fps, H.265, RTSP/TCP
- 계측 환경변수:
  - `GSTAPP_RTSP_SYNC_TRACE_SEC=60`
  - `GSTAPP_RTSP_FRAME_ID_SEI=1`
- client 4개를 병렬 연결하고 75초 동안 수신했다.
- RTSP/TCP packet 앞 512byte와 RTCP SR을 capture했다.
- 최종 계측 바이너리 checksum:
  `db7799a46fd077945923490295258ac435c5e0e541e46ae1f31a86c260af22a6`

#### 결과: server 삽입 및 기존 client 호환성

| 채널 | 60초 bridge AU | push 성공 | SEI 성공 | SEI 실패 | 모든 drop/실패 |
| --- | ---: | ---: | ---: | ---: | ---: |
| ch0 | 900 | 900 | 900 | 0 | 0 |
| ch1 | 900 | 900 | 900 | 0 | 0 |
| ch2 | 900 | 900 | 900 | 0 | 0 |
| ch3 | 900 | 900 | 900 | 0 | 0 |

- 기존 PTS는 계속 제거됐으며 appsrc/payloader PTS 역행과 RTP timestamp 역행은
  모두 0이었다.
- 기존 `rtph265depay ! h265parse` client도 70초 시점에 채널별
  1,043~1,046 access unit을 지속 수신해 decoder/parser 호환 문제가 없었다.

#### 결과: client Frame ID 추출

| 채널 | 추출 ID 수 | 첫 ID | 마지막 ID | payload 오류 |
| --- | ---: | ---: | ---: | ---: |
| ch0 | 1,120 | 81 | 1,201 | 0 |
| ch1 | 1,122 | 80 | 1,201 | 0 |
| ch2 | 1,119 | 83 | 1,201 | 0 |
| ch3 | 1,119 | 82 | 1,201 | 0 |

- version, payload channel, frame ID와 원본 PTS 관계 오류는 모두 0이었다.
- 접속 시작 ID는 ch1=80, ch0=81, ch3=82, ch2=83으로 media 준비 순서에
  따라 최대 3프레임 차이가 났다.
- 네 채널 공통 ID는 **84..1201의 연속된 1,118개**였다.
- 공통 ID 1,118개 모두 네 채널의 `origin_pts_ns`가 bit 단위로 같았고
  불일치는 0이었다. 따라서 영상 내용 비교 없이 같은 원본 프레임임을 직접
  식별했다.
- pcap은 앞 512byte만 저장해 ch0/ch3의 각 1개 SEI가 TCP batch 뒤쪽에서
  잘렸다. 이는 capture truncation이며 공통 ID 범위에는 누락이 없고 server
  삽입 실패도 0이었다.

#### 결과: 동일 Frame ID의 RTCP NTP 시간차

SEI가 들어 있는 RTP packet timestamp를 RTCP SR로 NTP 시각에 변환하고,
공통 frame ID 1,118개에서 네 채널의 시간 spread를 계산했다.

| 지표 | 결과 |
| --- | ---: |
| 중앙값 | 12.662ms |
| P95 | 18.364ms |
| P99 | 23.637ms |
| 최대 | 62.012ms |
| 반 프레임(33.333ms) 미만 | 1,115 / 1,118 |
| 한 프레임(66.667ms) 미만 | 1,118 / 1,118 |

- 75초 수신 구간의 모든 공통 ID에서 1프레임 이상의 송출 시각 차이는 없었다.
- 3개 frame ID는 반 프레임보다 늦었지만 최대 62.012ms로 한 프레임 이내였다.
- loopback TCP 도착 spread도 중앙값 12.640ms, P95 18.958ms, P99
  24.276ms, 최대 62.132ms로 RTCP 환산 결과와 유사했다.

#### 판정 및 한계

- 공통 frame ID로 client가 동일 원본 프레임을 직접 식별할 수 있게 됐다.
- RTCP SR과 결합하면 동일 frame ID의 절대 송출 시간차도 계산할 수 있다.
- 이번 구간에서는 모든 공통 frame이 한 프레임 이내였지만 최대값 62ms는
  한 프레임 66.7ms에 가깝다. 장시간 및 CPU/VPU/TCP 부하 조건의 분포를 추가
  확인해야 한다.
- 접속 시작 frame은 여전히 최대 3개 다르다. 첫 화면부터 정렬하려면 client가
  네 stream에서 모두 존재하는 첫 공통 ID를 찾을 때까지 buffering하는
  시작 barrier가 필요하다.
- 현재 구현은 H.265에 한정된다. H.264 사용 시 별도 SEI 구현이 필요하다.
- 이 frame ID는 동일 V4L2 lineage를 나타내며 센서 노출 시각의 직접 증거는
  아니다.

#### 코드, 원시 로그 및 원복

- 구현: `rtspServerBin.cpp`, `rtspServerBin.h`, `Makefile`
- 최종 자료:
  - `tmp/target-192.168.214.4/rtsp_frame_id_final.pcapng`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_packets.txt`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_sr.csv`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_analysis.json`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_analysis.txt`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_trace.log`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_measurement_status.log`
  - `tmp/target-192.168.214.4/rtsp_frame_id_final_restore_status.log`
- timestamp rebase 실패 시험은 비교 근거로 `rtsp_preserve*` 파일에 보존했다.
- 측정 후 운영 binary/process checksum을
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. 서비스 active, 환경변수 0개, video FD 4개를 확인하고 타겟 임시
  파일을 삭제했다.

### 3.12 4채널 공통 Frame ID 시작 barrier client 실측

#### 목적

3.11에서는 packet capture를 사후 분석해 공통 Frame ID를 확인했다. 이번에는
실제 RTSP client가 네 channel을 동시에 수신하면서 H.265 SEI를 직접 해석하고,
네 queue에 모두 존재하는 최초 공통 ID부터 frame group을 생성할 수 있는지
검증했다. 영상 화소 비교 없이 client 수신단에서 같은 원본 frame을 선택하는
것이 목표다.

#### client 구현 및 정렬 방식

- 독립 검증 프로그램 `test/rtspFrameSyncClient.cpp`와 build target
  `bin/rtspFrameSyncClient`를 추가했다.
- 하나의 process와 `GstPipeline` 안에 다음 4개 수신 chain을 구성했다.
  - `rtspsrc` RTSP/TCP, `latency=100`
  - `rtph265depay ! h265parse`
  - byte-stream/AU alignment `appsink`
- 각 AU에서 `GSTSYNC1` SEI의 version, channel, `frame_id`, `origin_pts_ns`를
  해석한다.
- 시작 시 각 channel queue의 선두 ID 중 최댓값을 목표로 정하고 더 오래된
  frame을 버리는 동작을 반복한다. 네 queue 선두 ID가 같아진 지점을 최초
  barrier로 선언한다.
- barrier 이후에도 같은 방식을 사용하되, 누락 때문에 버린 frame은
  `alignment_drop`으로 별도 집계한다.
- channel별 ID gap/duplicate, 잘못된 SEI, 최초 정렬 drop, 운용 중 정렬 drop과
  동일-ID group의 client 도착 시간 및 client PTS spread를 기록한다.

#### 시험 환경 및 실행 이력

- 타겟: `192.168.214.4`
- 입력/송출: 4채널 FHD 15fps, H.265, RTSP/TCP, `/ch0`~`/ch3`
- server 환경변수:
  - `GSTAPP_RTSP_SYNC_TRACE_SEC=60`
  - `GSTAPP_RTSP_FRAME_ID_SEI=1`
- server checksum:
  `db7799a46fd077945923490295258ac435c5e0e541e46ae1f31a86c260af22a6`
- client checksum:
  `c777bca41b1de5b85502a765faabc7423b32ce868499c927eff8124bbb5767c0`
- 최종 유효 측정은 client 자체 timeout 30초로 수행했다. 실제 monotonic 경과는
  30.444초였다.
- 최초 60초 측정은 client 수신 약 41초 시점에 타겟 외부 `killcam` supervisor가
  시험 server를 SIGTERM으로 재시작하여 조기 종료됐다. 종료 전까지 공통 ID
  4..615의 연속 612 group, channel gap/duplicate/invalid SEI/alignment drop 0을
  확인했지만, 지정 시간을 완주하지 않았으므로 최종 시간 통계에서는 제외했다.
  이 자료는 `*_server_restart_trial*` 이름으로 보존했다.

#### 최종 결과

최초 수신 ID는 ch0=49, ch1=50, ch2=51, ch3=52였다. client는 ch0에서 3개,
ch1에서 2개, ch2에서 1개를 시작 정렬용으로 버리고 **ID 52**에서 barrier를
성립시켰다.

| 채널 | 수신 frame | 첫 ID | 마지막 ID | gap | duplicate | 시작 drop | 운용 중 drop | invalid SEI |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ch0 | 456 | 49 | 504 | 0 | 0 | 3 | 0 | 0 |
| ch1 | 455 | 50 | 504 | 0 | 0 | 2 | 0 | 0 |
| ch2 | 454 | 51 | 504 | 0 | 0 | 1 | 0 | 0 |
| ch3 | 452 | 52 | 503 | 0 | 0 | 0 | 0 | 0 |

- 공통 group은 **52..503의 연속 452개**다. 범위의 이론적 개수도 452이므로
  group ID 또는 원본 PTS 불일치는 0이다.
- barrier 성립 후 `alignment_drop`은 네 channel 모두 0이었다. 즉 최종 구간에는
  같은 원본 frame group을 만들지 못한 frame이 없었다.
- 측정 종료 순간의 thread 도착 순서 때문에 ch0~ch2 queue에 ID 504가 각 1개
  남았고 ch3에는 없었다. 이는 timeout 경계의 미완성 group이며 누락 판정에
  포함하지 않았다.

| 동일-ID 4채널 spread | 최소 | 평균 | 최대 |
| --- | ---: | ---: | ---: |
| client monotonic 도착 시각 | 8.994ms | 14.293ms | 78.337ms |
| client buffer PTS | 10.463ms | 14.784ms | 77.911ms |

- 평균 도착 spread는 15fps 한 frame(66.667ms)의 0.214배였다.
- 최대 도착/PTS spread는 약 1.17 frame으로 한 frame을 넘었다. 이것은 같은
  Frame ID의 TCP 전달 및 각 RTSP session scheduling 시각 차이다. 공통 ID와
  `origin_pts_ns`는 같았으므로 센서/capture frame이 한 장 어긋났다는 의미가
  아니다.
- 따라서 frame-aligned 소비자는 독립 RTSP PTS만 즉시 재생하지 말고, 이번
  client처럼 공통 ID barrier와 짧은 queue를 사용해야 한다.

#### 판정, 원시 로그 및 원복

- standalone client에서 공통-ID 시작 barrier가 실제 동작하고, 시작 시 최대
  3 frame 차이를 흡수한 뒤 452개 연속 group을 생성함을 확인했다.
- 이 client는 검증용 구현이다. 제품 client에 적용하려면 queue 상한, channel
  timeout, 불완전 group 폐기, reconnect 시 barrier 재설정 정책이 추가로 필요하다.
- 원시 자료:
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client.log`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_status.log`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_server_trace.log`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_restore.log`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_analysis.txt`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_server_restart_trial.log`
  - `tmp/target-192.168.214.4/rtsp_frame_sync_client_server_restart_trial_status.log`
- 최종 측정 후 운영 binary와 process를 checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. `cam-operate.service=active`, 시험 환경변수 0개, video FD 4개,
  `/tmp/start_video*` marker 1개를 확인했다.

### 3.13 단일 RTSP channel 재접속과 barrier 재수립

#### 목적

4채널 공통-ID group 생성 도중 ch2를 의도적으로 disconnect/reconnect하고,
client가 접속 단절 전의 불완전 queue를 폐기한 뒤 두 번째 공통-ID barrier를
세우는지 확인했다.

#### client 재접속 기능

`rtspFrameSyncClient`에 다음 시험 option을 추가했다.

- `--reconnect-channel`: 재접속할 channel
- `--reconnect-after`: client 시작 후 disconnect 시각(초)
- `--reconnect-gap`: disconnect 유지 시간(초)

재접속을 시작하면 모든 channel queue를 새 epoch로 전환하고 group 생성을
중지한다. 대상 channel의 새 session에서 첫 Frame ID를 받으면 다른 channel의
더 오래된 queue를 버리고 네 queue 선두가 같은 최초 ID에서 barrier를 다시
성립시킨다. 의도적인 단절 구간의 ID는 일반 `gap`과 구분해
`reconnect_skipped_ids`로 기록한다.

#### 1차 방식 실패와 수정

처음에는 하나의 `GstPipeline` 안에서 ch2 `rtspsrc` element만
NULL→PLAYING으로 전환했다. server에서는 ch2 disconnect와 새 connect가 모두
관찰됐지만, 처음 만들어진 delayed dynamic-pad link가 재사용되지 않아 client가
다음 오류로 종료됐다.

```text
streaming stopped, reason not-linked (-1)
```

따라서 단순히 기존 `rtspsrc`의 state만 재기동하는 방식은 제외했다. 최종
client는 channel별로 독립 `GstPipeline`을 구성하고, disconnect된 channel의
`rtspsrc ! depay ! parser ! appsink` pipeline 전체를 제거하고 다시 생성한다.

#### 시험 환경

- 4채널 FHD 15fps, H.265, RTSP/TCP, `latency=100ms`
- 전체 측정: 지정 30초, monotonic 실측 30.070초
- ch2 disconnect: 시작 후 8.078초
- ch2 reconnect 요청: 시작 후 11.071초
- server 환경변수:
  - `GSTAPP_RTSP_SYNC_TRACE_SEC=60`
  - `GSTAPP_RTSP_FRAME_ID_SEI=1`
- server checksum:
  `db7799a46fd077945923490295258ac435c5e0e541e46ae1f31a86c260af22a6`
- client checksum:
  `50c97701eac139572f99fd2bf98c9567aeb0932f402d12a7580725ef0db1af74`

#### 결과

| 구간 | barrier ID | 공통 group | 개수 | 시작 정렬 drop(ch0,ch1,ch2,ch3) |
| --- | ---: | --- | ---: | --- |
| epoch 0, 재접속 전 | 51 | 51..167 | 117 | 0,3,2,1 |
| epoch 1, 재접속 후 | 214 | 214..498 | 285 | 46,46,0,46 |

- ch2는 ID 167까지 group을 만든 뒤 재접속 첫 frame으로 ID 214를 받았다.
- 의도적인 단절 구간은 ID 168..213의 **46개**다.
- ch0, ch1, ch3 queue에서 각각 46개 오래된 frame을 버리고 ID 214에서 두 번째
  barrier를 성립시켰다.
- 재접속 후 ID 214..498은 이론적 개수와 같은 연속 285 group이다.
- 두 epoch의 유효 group은 총 402개이고 `frame_id/origin_pts_ns` 불일치는 0이다.
- 전 channel의 비의도적 gap, duplicate, invalid SEI 및 barrier 성립 후
  `alignment_drop`은 모두 0이다.
- server log에서도 ch2 media unprepared/disconnect 후 약 3초 뒤 ch2 connect와
  media connect가 확인됐다. 다른 세 channel은 측정 종료까지 유지됐다.

| 동일-ID 4채널 monotonic 도착 spread | 결과 |
| --- | ---: |
| 최소 | 7.892ms |
| 평균 | 14.113ms |
| 최대 | 72.602ms |

client PTS spread 최대값은 11.158초였다. 새 ch2 RTSP pipeline의 PTS가 다시
0 근처에서 시작한 반면 나머지 session PTS는 계속 증가했기 때문이다. 이는
재접속 후 독립 RTSP PTS 절대값을 channel 간 동일 frame 판정에 사용할 수
없음을 보여준다. 공통 Frame ID는 PTS reset과 관계없이 정상 정렬됐다.

#### 판정, 원시 로그 및 원복

- 대상 channel pipeline 전체 재생성과 새 공통-ID barrier 방식으로 단일
  channel 재접속을 복구할 수 있다.
- 제품 client에서는 각 channel에 독립 pipeline lifecycle을 두고, disconnect
  시 모든 미완성 group을 폐기한 뒤 새 epoch를 시작해야 한다.
- 최종 자료:
  - `tmp/target-192.168.214.4/rtsp_reconnect_client.log`
  - `tmp/target-192.168.214.4/rtsp_reconnect_status.log`
  - `tmp/target-192.168.214.4/rtsp_reconnect_server_trace.log`
  - `tmp/target-192.168.214.4/rtsp_reconnect_restore.log`
  - `tmp/target-192.168.214.4/rtsp_reconnect_analysis.txt`
- 제외한 1차 실패 자료는 같은 디렉터리의
  `rtsp_reconnect_*_trial1.log`에 보존했다.
- 측정 후 운영 binary/process checksum을
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. service active, 시험 환경변수 0개, video FD 4개와 시작 marker
  1개를 확인했다.

### 3.14 ch2 3회 반복 재접속과 다중 epoch 검증

#### 목적과 client 확장

3.13의 단일 channel pipeline 재생성 방식이 반복 장애에서도 누적 오류 없이
동작하는지 확인했다. `rtspFrameSyncClient`를 최대 7회의 재접속 epoch를
기록하도록 일반화하고 다음 option을 추가했다.

- `--reconnect-count`: 재접속 반복 횟수
- `--reconnect-interval`: reconnect 성공 후 다음 disconnect까지의 시간

각 반복은 대상 pipeline 제거, 모든 미완성 queue 폐기, epoch 증가, pipeline
재생성, 최초 공통 ID barrier 순서로 진행한다. epoch별 barrier ID, 연속 group,
시작 drop 및 의도적 skip을 별도로 출력한다.

#### 시험 환경과 설정

- 4채널 FHD 15fps, H.265, RTSP/TCP, `latency=100ms`
- 측정: 지정 35초, monotonic 실측 35.701초
- 대상: ch2
- 첫 disconnect: 시작 후 약 7.7초
- disconnect 유지: 매회 약 2초
- reconnect 후 다음 disconnect까지: 약 5초
- 반복 횟수: 3회
- 실행 option:
  - `--reconnect-channel 2`
  - `--reconnect-after 7`
  - `--reconnect-gap 2`
  - `--reconnect-count 3`
  - `--reconnect-interval 5`
- server 환경변수:
  - `GSTAPP_RTSP_SYNC_TRACE_SEC=60`
  - `GSTAPP_RTSP_FRAME_ID_SEI=1`
- server checksum:
  `db7799a46fd077945923490295258ac435c5e0e541e46ae1f31a86c260af22a6`
- client checksum:
  `d9829dd4c2718632b574eb82354edff4661f804382405c3fe0834ee545590924`

#### epoch별 결과

| epoch | 구간 | barrier ID | 공통 group | 개수 | ch2 의도적 skip | 시작 drop(ch0,ch1,ch2,ch3) |
| ---: | --- | ---: | --- | ---: | ---: | --- |
| 0 | 최초 접속 | 51 | 51..161 | 111 | 0 | 3,2,0,1 |
| 1 | 1차 재접속 | 193 | 193..266 | 74 | 30 | 30,30,0,30 |
| 2 | 2차 재접속 | 298 | 298..371 | 74 | 30 | 30,30,0,30 |
| 3 | 3차 재접속 | 403 | 403..581 | 179 | 30 | 31,31,0,30 |

- 재접속 요청과 성공은 3/3, barrier는 총 4회 성립했다.
- 네 epoch의 공통 group은 총 **438개**다. 각 ID 범위의 이론적 개수와 실제
  group 수가 같고 `frame_id/origin_pts_ns` 불일치는 0이다.
- ch2의 의도적 skip은 매회 30개, 총 90개로 분리 집계됐다.
- 전 channel의 비의도적 gap, duplicate, invalid SEI 및 barrier 이후
  `alignment_drop`은 모두 0이다.
- 측정 종료 시 네 queue의 잔류 frame도 모두 0이었다.
- server에서도 ch2 media unprepared/disconnect 및 connect/media connect가
  각각 세 번 확인됐다.

| 동일-ID 4채널 monotonic 도착 spread | 결과 |
| --- | ---: |
| 최소 | 6.861ms |
| 평균 | 14.056ms |
| 최대 | 56.277ms |

최대 도착 spread도 15fps 한 프레임 66.667ms 이내였다. 반면 client PTS spread
최대값은 23.792초까지 증가했다. ch2 pipeline을 재생성할 때마다 새 독립 RTSP
PTS가 시작하고 다른 세 session은 계속 증가하기 때문이다. 반복 재접속에서도
공통 Frame ID는 이 PTS reset과 무관하게 정상 동작했다.

#### 판정, 원시 로그 및 원복

- channel pipeline 전체 재생성과 epoch별 공통-ID barrier가 동일 channel
  3회 반복 장애에서도 안정적으로 동작했다.
- 원시 자료:
  - `tmp/target-192.168.214.4/rtsp_repeat_reconnect_client.log`
  - `tmp/target-192.168.214.4/rtsp_repeat_reconnect_status.log`
  - `tmp/target-192.168.214.4/rtsp_repeat_reconnect_server_trace.log`
  - `tmp/target-192.168.214.4/rtsp_repeat_reconnect_restore.log`
  - `tmp/target-192.168.214.4/rtsp_repeat_reconnect_analysis.txt`
- 측정 후 운영 binary/process checksum을
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. service active, 시험 환경변수 0개, video FD 4개와 시작 marker
  1개를 확인했다.

### 3.15 RTSP/TCP backpressure의 프레임 폐기 및 자동 재정렬

#### 목적

한 RTSP 채널의 downstream이 일시 정체될 때 다음 동작을 검증했다.

1. 서버가 정체 중 과거 프레임을 무한 적재하는지, 제한된 queue에서 폐기하는지
2. 서버 queue 폐기 수와 client Frame ID gap이 일치하는지
3. 정체 해제 뒤 최신 구간으로 복귀하고 네 채널 공통 group이 다시 생성되는지

#### 환경 및 계측 확장

- 타겟: `192.168.214.4`
- 영상: 4채널 FHD 15fps, H.265, RTSP/TCP, `/ch0`~`/ch3`
- server checksum:
  `078a90978cef9a2e7048a1966c84bae030dd0d3f8f93bc806704c7e9163b49e8`
- client checksum:
  `281d05f8930684a0b8182463ab1778dde7355931b7f163483ded12c06ee653ed`
- RTSP factory queue: `max-size-buffers=2`, `leaky=2`(downstream)
- server 환경변수:
  - `GSTAPP_RTSP_SYNC_TRACE_SEC=30`
  - `GSTAPP_RTSP_FRAME_ID_SEI=1`
  - `GSTAPP_RTSP_TEST_STALL_CH=2`
  - `GSTAPP_RTSP_TEST_STALL_AFTER_SEC=7`
  - `GSTAPP_RTSP_TEST_STALL_DURATION_SEC=10`
- client 측정 시간: 40초

video factory queue에 `rtsp_out_queue` 이름을 부여하고 `overrun` signal을
`factory_queue_overrun`으로 집계했다. client에는 channel별 연속 Frame ID의
gap 범위와, gap 뒤 생성된 공통 group 수를 추가했다.

실제 TCP 정체를 결정적으로 만들기 위해 시험 환경변수가 모두 설정된 경우에만
ch2 `pay0` src pad를 시작 7초 뒤 10초 동안 block하는 probe를 추가했다. 이는
제품 동작이 아니라 계측용 fault injection이며 기본 실행에서는 비활성이다.

#### 예비시험: client callback 지연의 한계

먼저 client ch2 appsink callback만 지연하는 두 시험을 수행했다.

| 시험 | 지연 설정 | server overrun/full drop | client 결과 |
| --- | --- | --- | --- |
| 1차 | 15초, callback당 1.5초 | 0 / 0 | gap 0, ID 160→376 적체분을 약 132ms에 처리 |
| 2차 | 30초, callback당 5초 | 0 / 0 | gap 0, ID 168→615 적체분을 약 267ms에 처리 |

2차에서는 bounded client queue가 상한에 도달해 네 채널 모두
`alignment_drop=148`이 발생했다. 그러나 server는 채널별 675 frame을 모두
push했고 `factory_queue_overrun`, `full_drop`, `push_fail`은 0이었다.

따라서 loopback에서는 TCP socket, `rtspsrc`와 client 내부 queue가 데이터를
수용하기 때문에 appsink callback 지연만으로 server backpressure를 보장할 수
없다. 이 조건에서는 연결 중 밀린 프레임이 자동으로 최신 한 장으로 갱신되는
것이 아니라, 버퍼 정책에 따라 적체된 과거 프레임이 해제 후 빠르게 전달된다.

#### 최종시험 결과

ch2 payloader downstream은 08:50:58부터 08:51:08까지 정확히 10초 정체됐다.
최종 자동 판정은 `VALIDATION_OK=1`이었다.

| 채널 | server queue overrun | client gap | gap event | alignment drop | duplicate/invalid SEI |
| --- | ---: | ---: | ---: | ---: | ---: |
| ch0 | 0 | 0 | 0 | 147 | 0 / 0 |
| ch1 | 0 | 0 | 0 | 147 | 0 / 0 |
| ch2 | **147** | **147** | 1 | 0 | 0 / 0 |
| ch3 | 0 | 0 | 0 | 147 | 0 / 0 |

- ch2 누락 범위는 **Frame ID 158..304**의 연속 147개였다.
- queue overrun 147회와 client에서 관찰한 Frame ID 누락 147개가 정확히
  일치했다.
- 정체 전 ID 52..157에서 106개, 정체 후 ID 305..655에서 351개, 총
  **457개의 공통 group**이 생성됐다.
- 정체 후 공통 group 351개가 별도 reconnect 또는 새 epoch 없이 계속 생성돼
  자동 재정렬을 확인했다.
- 전체 `group_mismatches=0`, server `push_fail=0`,
  `frame_id_sei_failed=0`이었다.
- ch0/ch1/ch3의 `alignment_drop=147`은 해당 채널 자체 손실이 아니다. ch2에
  없는 ID를 네 채널 공통 group에서 제외한 수다.

`full_drop=0`인 것은 이번 결과와 모순되지 않는다. `full_drop`은 appsrc의
`enough-data` 상태에서 bridge 입력을 버린 횟수다. 이번 정체는 appsrc 뒤의
leaky factory queue가 흡수했으므로 직접 지표는 `factory_queue_overrun`이다.

#### 판정과 한계

- 크기 2의 downstream-leaky queue는 정체 중 오래된 frame을 폐기하고 정체
  해제 뒤 약 10초치 과거 frame을 재생하지 않고 최신 구간으로 복귀했다.
- Frame ID barrier는 단일 channel gap을 감지하고 나머지 channel의 같은 수의
  frame을 정렬 폐기한 뒤 자동으로 공통 group 생성을 재개했다.
- 검증 client는 H.265 AU를 parse하지만 실제 decode하지 않는다. 정체 해제 후
  첫 AU가 delta frame이면 실제 화면 정상화는 다음 IDR까지 늦을 수 있으므로
  decoder가 포함된 시각적 복구 시간은 아직 증명하지 않았다.
- 실제 외부 TCP 혼잡, CPU/VPU 부하, 여러 channel 동시 정체와 장시간 queue/
  memory 상한은 별도 검증이 필요하다.

#### 원시 로그 및 원복

- 최종 자료:
  - `tmp/target-192.168.214.4/rtsp_backpressure_client.log`
  - `tmp/target-192.168.214.4/rtsp_backpressure_server_trace.log`
  - `tmp/target-192.168.214.4/rtsp_backpressure_status.log`
  - `tmp/target-192.168.214.4/rtsp_backpressure_restore.log`
  - `tmp/target-192.168.214.4/rtsp_backpressure_analysis.txt`
- client 지연 예비시험은 같은 디렉터리의 `_trial1.log`, `_trial2.log` 파일로
  보존했다.
- 측정 후 운영 binary/process checksum을
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`로
  복구했다. `cam-operate.service=active`, 시험 환경변수 0개, video FD 4개를
  확인했고 임시 binary, script와 backup을 삭제했다.

### 3.16 `vpudec` decoder 정체 후 복귀 1차 시험

#### 목적 및 방법

Frame ID 정렬 복구가 실제 H.265 decoder에서도 오류 없이 유지되는지 확인하기
위해 타겟의 `vpudec`를 사용했다. 네 channel에 다음 수신 pipeline을 동시에
구성했다.

```text
rtspsrc(protocols=tcp, latency=100) ! rtph265depay ! h265parse ! vpudec ! fakesink
```

서버는 ch2 payloader downstream을 시작 7초 뒤 10초간 정체시켰고, decoder
pipeline은 약 32초 동안 유지했다. 이 시험은 `gst-launch-1.0`의 `fakesink`로
decoded buffer를 소비하므로 화면 화소 자체는 저장하지 않는다.

#### 결과

- 네 channel 모두 `vpudec` 초기화 메시지를 출력했다.
- `vpudec` 오류, EOS, `not-negotiated`, pipeline 중단 메시지는 관찰되지 않았다.
- ch2 정체 시작/종료는 server log에서 확인됐다.
- server 운영 binary는 종료 후 원본 checksum으로 복구됐다.
- `cam-operate.service=active`, video FD 4개를 확인했다.

이번 1차 시험에서는 `fakesink`의 decoded buffer 개수와 첫 IDR 시각을 별도
callback으로 계측하지 않았으므로, **decoder가 오류 없이 pipeline을 유지했다는
것만 확인**할 수 있다. 따라서 정체 종료 후 첫 정상 화면까지의 정확한 시간은
아직 미확정이다. 다음 decoder 시험에서는 `appsink` callback과 H.265 NAL
분석을 추가해 IDR 및 decoded-buffer 시각을 직접 기록해야 한다.

원시 자료:

- `tmp/target-192.168.214.4/decoder_test.log`
- `tmp/target-192.168.214.4/decoder_test_server.log`

### 3.17 `vpudec ! identity ! fakesink` decoded-buffer 계측 재시험

#### 목적

3.16의 `vpudec` 초기화 성공만으로는 실제 decoded buffer가 정체 전후에
출력됐는지 알 수 없으므로, decoder 뒤에 `identity`를 삽입하고 buffer 흐름을
추가 확인했다.

```text
rtspsrc ! rtph265depay ! h265parse ! vpudec ! identity(silent=false) ! fakesink
```

#### 결과

- 네 channel 모두 `vpudec` 초기화 및 RTSP PLAYING 전환에 성공했다.
- `vpudec` 오류, EOS, `not-negotiated` 메시지는 없었다.
- ch2 정체 시작/종료는 각각 `00:11:50.636`/`00:12:00.636`에 관찰됐다.
- 그러나 `gst-launch-1.0` 로그에는 `identity` buffer handoff/timestamp가
  출력되지 않았다. `identity`의 표준 로그만으로는 decoded buffer 개수를
  신뢰성 있게 산출할 수 없었다.

따라서 이 시험은 decoder pipeline이 오류 없이 유지된다는 사실만 보강하며,
첫 IDR 및 첫 decoded buffer의 정확한 시간은 아직 측정하지 못했다. 이를
완료하려면 `GstAppSink` 또는 `identity`의 `handoff` callback을 직접 연결한
C/C++ 계측 client가 필요하다. 현재 결과를 decoder 화면 복구 성공으로
과대해석하지 않는다.

원시 자료:

- `tmp/target-192.168.214.4/decoder_identity.log`
- `tmp/target-192.168.214.4/decoder_identity_server.log`

### 3.18 `GstAppSink` 기반 실제 decoded buffer callback 시험

#### 구현

`test/decoderRecoveryClient.cpp`와 `bin/decoderRecoveryClient`를 추가했다.
수신 pipeline은 다음과 같다.

```text
rtspsrc ! rtph265depay ! h265parse ! vpudec ! appsink
```

`appsink::new-sample` callback에서 decoded buffer 개수, 첫/마지막 PTS와
monotonic 경과 시간을 직접 집계한다.

#### 시험 조건

- 타겟: `192.168.214.4`
- ch2 downstream 정체: 시작 7초 후 10초
- client 실행 시간: 40초
- decoder client checksum:
  `2f34501b1421efa83eee541d036e3903087dac300fc081e67602280a6d950a1e`
- server test checksum:
  `078a90978cef9a2e7048a1966c84bae030dd0d3f8f93bc806704c7e9163b49e8`

#### 결과

```text
DECODER_FIRST pts=15428318851 mono_ns=75089583539000
DECODER_SUMMARY samples=214 first_pts=15428318851 last_pts=40574640881 elapsed_ns=25149996000
RC=0
```

- `vpudec` 초기화 성공
- 첫 decoded buffer callback 수신 성공
- decoded samples: **214개**
- 첫 decoded PTS: `15428318851ns`
- 마지막 decoded PTS: `40574640881ns`
- callback 관찰 구간: 약 **25.150초**
- client 오류/비정상 종료: 없음(`RC=0`)
- server ch2 정체: `00:15:40.704`~`00:15:50.704`

이번 결과로 `vpudec` 뒤의 실제 `appsink new-sample` callback이 동작하고,
정체 시험 중에도 decoder pipeline이 종료되지 않는 것을 확인했다. 다만 현재
client는 decoder 출력 buffer에 Frame ID SEI가 남아 있는지와 IDR 여부를
분석하지 않으므로, **첫 IDR까지의 시간 및 화면 복구 완료 시점은 아직 분리
측정하지 못했다**. 또한 214개 sample은 client 연결 준비 및 decoder 내부
latency를 제외한 callback 관찰 결과이므로 40초 전체의 이론적 600 frame과
직접 비교하지 않는다.

원시 자료:

- `tmp/target-192.168.214.4/decoder_recovery.log`
- `tmp/target-192.168.214.4/decoder_recovery_rc`
- `tmp/target-192.168.214.4/decoder_recovery_server.log`

### 3.19 decoder callback 반복 시험 및 최종 상태

동일한 ch2 정체 조건으로 decoder callback client를 반복 실행했다.

- 1차 반복: `samples=203`, callback 경과 약 `24.369s`, `RC=0`
- 2차 반복: decoder 초기화 후 `DECODER_FIRST pts=0`에서 시험 supervisor가
  종료되어 전체 sample summary를 만들지 못했다. 이 표본은 완주 시험으로
  판정하지 않고 제외한다.

1차 반복은 `vpudec` 초기화와 decoded callback 수신이 재현됐고 오류 없이
종료됐다. 2차 표본은 완주하지 못했으므로 성공 결과에 포함하지 않았다.

최종 타깃 확인:

- 운영 checksum:
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
- `cam-operate.service=active`
- `GSTAPP_RTSP_*` 시험 환경변수 0개
- `/dev/video` FD 4개

원시 자료:

- `tmp/target-192.168.214.4/decoder_repeat_1.log`
- `tmp/target-192.168.214.4/decoder_repeat_2.log`
- `tmp/target-192.168.214.4/decoder_repeat_1.rc`

### 3.20 H.265 IDR 직접 검출을 포함한 최종 decoder 시험

#### 구현

`decoderRecoveryClient`를 수정해 `h265parse` 뒤에 `tee`를 두고 한 branch에서
H.265 Annex-B NAL type 19/20(IDR)을 직접 검출했다. 다른 branch는
`vpudec ! appsink`로 실제 decoded buffer를 세었다.

#### 최종 시험 결과

시험 조건은 이전과 동일하게 ch2 downstream을 시작 7초 후 10초 정체시켰다.

```text
DECODER_FIRST pts=15482208622 mono_ns=76222088406000
DECODER_IDR first_ns=76222088541000
DECODER_SUMMARY samples=208 idr_count=2 first_idr_ns=76222088541000 first_pts=15482208622 last_pts=40090991657 elapsed_ns=24661189000
```

- decoded samples: **208개**
- 검출 IDR: **2개**
- 첫 decoded buffer와 첫 IDR callback 차이: 약 **1.35ms**
- decoded callback 관찰 구간: 약 **24.661초**
- decoder client 종료 코드: 0
- ch2 정체: `00:34:33.211`~`00:34:43.211`
- decoder 오류/EOS/not-negotiated: 0

첫 IDR은 decoder stream 시작 직후에 검출됐다. 이번 client는 compressed
branch와 decoded branch의 독립 callback만 기록하므로 정체 종료 후의 **복구용
IDR**를 별도 연결해 식별하지는 못했다. 따라서 “정체 종료 이후 첫 IDR까지의
시간”은 이 시험에서도 확정하지 않는다. 다만 정체 구간을 포함해 decoder가
계속 실행되고 decoded callback을 생성했으며, 실제 IDR 2개가 수신됐다는 것은
확인했다.

#### 최종 원복

- 운영 checksum:
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
- `cam-operate.service=active`
- 시험 환경변수 0개
- video FD 4개

원시 자료:

- `tmp/target-192.168.214.4/decoder_idr_final.log`
- `tmp/target-192.168.214.4/decoder_idr_server.log`

### 3.21 검증 코드 보완 사항 반영

decoder 검증 client에 다음 보완을 반영했다.

- H.265 Annex-B 3바이트 및 4바이트 start code 모두 IDR 검출
- `g_timeout_add_seconds()`용 반환형 안전 wrapper 추가
- GStreamer bus `ERROR`, `WARNING`, `EOS` 메시지 출력
- decoder callback 계측 경고 제거

cross-build와 `git diff --check`를 통과했다. 이 변경은 검증 client에 한정되며
운영 `gstApp` 동작에는 영향을 주지 않는다.

### 3.22 정체 구간 복구 시간 계측 보완

서버 시험용 stall 로그에 monotonic 시각(`mono_ns`)을 추가했다. 이제 stall
시작/종료와 client의 IDR/decoded callback monotonic 시각을 동일 시간축으로
비교할 수 있다. cross-build는 성공했고 `git diff --check`도 통과했다.

다음 실제 타깃 실행에서 `stall_end_mono_ns` 이후 첫 IDR 및 첫 decoded buffer를
자동 계산하면, 기존에 미측정으로 남아 있던 복구 시간을 정량화할 수 있다.

### 3.23 외부망 Frame ID baseline 재검증 및 원인 정정

#### 이전 외부망 시험의 무효 원인

초기 host-native 외부망 시험에서 `invalid_sei`가 증가했지만, 당시 타깃에는
원본 운영 binary가 실행 중이었다. 원본 binary에는 Frame ID SEI 삽입 계측
코드가 없으므로 `GSTAPP_RTSP_FRAME_ID_SEI=1` 환경변수만 설정해도 SEI가
생성되지 않는다. 따라서 해당 시험은 host parser 호환성 판정 자료로 사용할
수 없으며 무효 처리했다.

#### 계측 binary 외부망 재시험

- 호스트: `192.168.214.3`, x86_64 GStreamer 1.20.3 client
- 타깃: `192.168.214.4`, 실제 Ethernet 경로
- server test checksum:
  `231709b9e056155855d68931e4f8eb3dc80f942801bf08731ffda43e29709823`
- 환경변수: `GSTAPP_RTSP_SYNC_TRACE_SEC=45`,
  `GSTAPP_RTSP_FRAME_ID_SEI=1`
- client 측정: 30초

결과:

- `CLIENT_RC=0`
- barrier ID: 2
- 공통 group: **278개**, ID 2..279
- `group_mismatches=0`
- 네 채널 invalid SEI: **0**
- 네 채널 gap/duplicate: **0**
- 초기 정렬 drop: ch0=2, ch1=0, ch2=2, ch3=2
- 동일-ID client 도착 spread: 평균 13.659ms, 최대 21.216ms

따라서 외부 Ethernet 경로에서도 host-native client가 Frame ID를 정상
해석하며, 이전 invalid SEI 결과는 parser 문제가 아니라 **SEI가 없는 원본
binary로 시험한 설정 오류**였다. host parser 수정은 진단용 옵션만 유지하고,
별도 raw parser 변경은 추가하지 않는다.

최종 원복:

- 운영 binary/process checksum:
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
- `cam-operate.service=active`
- 시험 환경변수 0개
- video FD 4개

원시 로그: `tmp/target-192.168.214.4/rtsp_external_instrumented.log`

### 3.24 전용 외부 client 장치 `192.168.214.8` baseline

#### 구성과 실행 방식

- RTSP server: `192.168.214.4`
- 전용 client: `192.168.214.8`, ARM64, `eth0`
- client binary checksum:
  `ecf59e63a44c9a9048af50b56b005e9611753a0eb657cf7489d7d0fd9ab4b5e7`
- server test checksum:
  `231709b9e056155855d68931e4f8eb3dc80f942801bf08731ffda43e29709823`
- 측정 시간: 30초

`cam-operate.service`가 실행하는 관리 script와 계측 server를 분리하기 위해
서비스를 중단하고 계측 `gstApp -d 22 -m 4`를 `/root`에서 수동 실행했다.
RTSP port와 process checksum을 확인한 뒤 `.8`에서 client를 실행했다. 시험
script는 최대 90초 timeout과 `trap` cleanup을 사용해 실패해도 원본 binary와
서비스를 복구하도록 구성했다.

#### 결과

- `CLIENT_RC=0`
- 최초 barrier ID: **98**
- 공통 group: **444개**, ID 98..541
- `group_mismatches=0`
- 전 채널 gap/duplicate/invalid SEI/alignment drop: **0**
- 초기 정렬 drop: ch0=0, ch1=2, ch2=1, ch3=3
- 동일-ID 도착 spread: 최소 8.400ms, 평균 11.730ms, 최대 38.826ms
- 동일-ID client PTS spread: 최소 7.085ms, 평균 14.855ms,
  최대 112.914ms

전용 외부 client에서도 444개 연속 Frame ID group을 손실 없이 생성했다.
도착 spread 최대값은 15fps 한 frame(66.667ms) 이내였다. PTS spread 최대값은
독립 RTSP session의 scheduling 영향으로 한 frame을 넘었지만 같은 Frame ID와
원본 PTS group 불일치는 없었다.

#### 원복

- 수동 계측 process 종료
- 운영 binary/process checksum:
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
- `cam-operate.service=active`
- 시험 환경변수 0개
- video FD 4개
- 임시 backup 0개

원시 로그:
`tmp/target-192.168.214.4/rtsp_external_client8.log`

### 3.25 외부망 50ms ± 10ms delay/jitter 시험

#### 시험 구성

`.4`와 `.8`의 i.MX8 kernel에는 `sch_netem`이 포함되지 않았다. 따라서 호스트
`.3`에서 TCP proxy(`:9554`→`.4:8554`)를 실행하고, proxy의 `.8` 방향 egress
인터페이스 `enp5s0`에 kernel netem을 적용했다.

```text
.8 RTSP client → .3 TCP proxy → .4 RTSP server
                         ↓
              netem delay 50ms 10ms
```

첫 hairpin routing 방식은 같은 물리 인터페이스로 들어온 packet을 같은
인터페이스로 다시 전달하지 못해 연결에 실패했으므로 무효 처리했다. 최종
시험은 proxy가 TCP 연결을 종단한 뒤 새 TCP 연결로 server에 전달한다.

#### 결과

- client 실행: 35초, `RC=0`
- qdisc: 56,055,157byte/51,325packet, kernel drop 0
- barrier ID: 5
- 공통 group: 325개
- `group_mismatches=0`, invalid SEI/duplicate=0
- ch2 초기 gap: ID 3 한 개
- ch0/ch1/ch3 gap: 0
- 동일 ID 도착 spread: 최소 300.388ms, 평균 8.939s, 최대 11.719s
- 종료 queue 잔류: ch0=147, ch1=150, ch2=0, ch3=132

50ms 평균 delay 자체보다 독립 TCP session에 packet jitter가 적용되면서 TCP
순서 복구와 stream별 처리량 편차가 커졌다. 가장 느린 ch2를 기다리느라 다른
channel queue가 약 132~150frame 누적됐다. Frame ID group 불일치는 없었지만,
현재 검증 client의 queue 상한 300만으로는 장시간 jitter 환경의 memory/latency
정책이 부족하다는 것을 보여준다.

원시 자료:

- `tmp/target-192.168.214.4/rtsp_external_delay_proxy_client.log`
- `tmp/target-192.168.214.4/rtsp_external_netem_delay_client.log`
- `tmp/target-192.168.214.4/rtsp_external_netem_delay_qdisc.log`

### 3.26 외부망 packet loss 0.1%/0.5%/1.0% 시험

호스트 `.3` TCP proxy의 `.8` 방향 egress에 netem loss를 적용했다. TCP 전송이므로
kernel에서 폐기된 packet은 TCP retransmission으로 복구될 수 있다.

| 설정 | kernel drop | 공통 group | Frame ID gap | 도착 spread 평균/최대 |
| --- | ---: | ---: | ---: | ---: |
| 0.1% | 16 | 443 | 0 | 12.930ms / 74.491ms |
| 0.5% | 73 | 449 | 0 | 13.454ms / 80.689ms |
| 1.0% | 151 | 451 | 0 | 18.224ms / 424.967ms |

- 세 조건 모두 `RC=0`, barrier 성립, `group_mismatches=0`이었다.
- 전 채널 Frame ID gap/duplicate/invalid SEI/alignment drop은 0이었다.
- loss 1%에서도 access unit 손실은 없었지만 TCP retransmission 지연으로 동일
  frame의 최대 도착 spread가 약 425ms(약 6.4frame)까지 증가했다.
- 따라서 RTSP/TCP는 이 구간의 packet loss를 복구했으나 실시간 채널 정렬
  latency는 packet loss에 민감했다.

원시 자료는 `rtsp_external_loss_0.1*`, `rtsp_external_loss_0.5*`,
`rtsp_external_loss_1.0*` 파일이다.

### 3.27 외부망 bandwidth 12Mbps/8Mbps 제한 시험

정상 4채널 proxy 전송량은 약 15Mbps였다. 이를 기준으로 `.3→.8` egress에
TBF를 적용해 12Mbps와 8Mbps로 제한했다.

| 설정 | 공통 group | channel gap(ch0/ch1/ch2/ch3) | alignment drop | 최대 도착 spread |
| --- | ---: | --- | --- | ---: |
| 12Mbps | 434 | 14 / 6 / 4 / 0 | 6 / 14 / 16 / 20 | 8.894s |
| 8Mbps | 240 | 36 / 50 / 18 / 3 | 36 / 22 / 54 / 69 | 18.941s |

- 두 조건 모두 RTSP session은 40초 동안 유지됐고 client `RC=0`이었다.
- TBF packet drop은 0이었지만 제한된 전송률이 TCP backpressure를 만들었다.
- 12Mbps 구간 server trace에서 factory queue overrun은
  ch0=14/ch1=6/ch2=4/ch3=0으로 client gap과 정확히 일치했다.
- 8Mbps에서는 총 107개의 channel gap과 181개의 alignment drop이 발생했고,
  가장 빠른 ch3 queue에 251frame이 남았다.
- `group_mismatches`, duplicate와 invalid SEI는 두 조건 모두 0이었다.

즉, 4채널 실제 bitrate 아래로 대역폭을 제한하면 연결은 유지되지만 server
leaky queue가 과거 frame을 폐기한다. Frame ID barrier는 잘못된 frame끼리
묶지 않지만 느린 channel을 기다리면서 client latency와 queue가 크게 증가한다.
제품 client에는 queue depth뿐 아니라 시간 기반 timeout과 불완전 group 폐기
정책이 필요하다.

원시 자료:

- `tmp/target-192.168.214.4/rtsp_external_bw_12mbit.log`
- `tmp/target-192.168.214.4/rtsp_external_bw_8mbit.log`
- 동일 이름의 `.rc`, `_qdisc.log`
- `tmp/target-192.168.214.4/rtsp_external_bw_server_trace.log`

### 3.28 H.265 Frame ID SEI server 자원 및 삽입 지연 A/B 시험

#### 목적과 환경

제품 server에서 Frame ID SEI를 상시 사용했을 때 CPU, memory, network, 온도와
frame별 처리시간에 주는 영향을 정량화했다.

- server: `192.168.214.4`, i.MX8, 4 CPU
- 외부 client: `192.168.214.8`
- 영상: 4채널 FHD 15fps, H.265, RTSP/TCP
- A/B server checksum:
  `231709b9e056155855d68931e4f8eb3dc80f942801bf08731ffda43e29709823`
- client checksum:
  `ecf59e63a44c9a9048af50b56b005e9611753a0eb657cf7489d7d0fd9ab4b5e7`
- 같은 server binary에서 `GSTAPP_RTSP_FRAME_ID_SEI`만 바꿨다.
- 1차: SEI OFF 30분 후 ON 30분
- 2차: 순서 효과를 확인하기 위해 ON 10분 후 OFF 10분
- process CPU/RSS는 `pidstat` 1초, PSS와 온도는 5초 간격으로 수집했다.
- client 종료 영향이 섞이지 않도록 `pidstat` 마지막 30초와 disconnect 뒤
  resource 표본을 비교에서 제외했다.

#### CPU, memory, network 결과

| 시험 | process CPU OFF→ON | system CPU OFF→ON | PSS 평균 OFF→ON | TX OFF→ON |
| --- | --- | --- | --- | --- |
| 30분 OFF→ON | 91.498→93.423%, **+1.925%p** | 41.064→41.695%, +0.630%p | 152.639→158.785MiB, +6.146MiB | 14.431→14.521Mbps, +89.889kbps |
| 10분 ON→OFF | 91.433→93.453%, **+2.020%p** | 41.306→41.824%, +0.518%p | 149.163→148.465MiB, -0.698MiB | 14.560→14.621Mbps, +61.451kbps |

- `pidstat` process CPU 100%는 CPU core 하나다. 순서를 뒤집어도 SEI ON 증가는
  +1.925~+2.020%p로 재현됐다. 4-core 전체 capacity 기준 약 +0.48~+0.51%p다.
- 전체 system CPU 증가는 +0.518~+0.630%p였다.
- PSS 평균은 30분 시험에서 증가했지만 역순 시험에서는 감소해 고정 SEI memory
  증가로 재현되지 않았다.
- 재시작 뒤 PSS는 두 조건 모두 증가했다. 30분 시작→종료는 OFF +19.486MiB,
  ON +21.215MiB였으므로 수시간 단위 memory plateau 확인은 여전히 필요하다.
- CPU/SoC 평균 온도 차이는 30분에서 +1.028/+1.067C였지만 역순에서
  -0.198/-0.240C로 바뀌어 독립적인 SEI 온도 상승으로 재현되지 않았다.
- TX 증가는 +61.451~+89.889kbps였다. 계산상 SEI/RTP/RTSP application
  overhead는 26.88kbps이며 나머지는 live encoder bitrate와 TCP batching 변동을
  포함한다.

#### 장시간 client 기능 결과

| 시험 | ON 공통 group | ID 범위 | gap/duplicate/invalid/mismatch | 도착 spread 평균/최대 |
| --- | ---: | --- | --- | ---: |
| 30분 | 27,298 | 72..27,369 | 모두 0 | 12.589/89.939ms |
| 역순 10분 | 9,302 | 72..9,373 | 모두 0 | 12.593/83.461ms |

- 두 실행 모두 시작 시 최대 3frame 차이를 폐기한 뒤 barrier가 성립했다.
- 종료 시 네 channel queue 잔류는 0이었다.
- 최대 spread는 한 frame 66.667ms를 넘었지만 같은 ID의 원본 PTS 불일치는
  없었다. 이는 원본 frame mismatch가 아니라 독립 TCP session의 도착 시각 차이다.
- SEI OFF의 `invalid_sei`와 client `RC=2`는 예상 결과다. 압축 AU는 계속
  수신했지만 Frame ID가 없어 barrier를 세우지 못했음을 뜻한다.

#### SEI 삽입 함수 수행시간

`rtsp_frame_id_sei_insert()` 호출 전후를 `gst_util_get_timestamp()`로 계측했다.

- timing server checksum:
  `7a5531d864cec2aecc168cb33498afcbd873314f553ceaa998a536813a433d4d`
- 측정: 120초, 전체 7,200회
- 평균: **86.877us**
- 최소/최대: **32.251us / 1.326793ms**
- 100us 이하: 5,651/7,200(78.49%)
- 250us 이하: 7,153/7,200(99.35%)
- 500us 초과 1ms 이하: 5회
- 1ms 초과: 1회

4채널 15fps의 초당 60회에 평균 시간을 곱하면 약 5.21ms CPU time/s, CPU core
하나의 약 0.52%다. 이는 SEI 생성/삽입 함수 구간만의 값이다. 전체 process
A/B의 약 +2.0%p에는 allocation, RTP 처리와 측정 변동이 함께 포함된다.

timing 실행에서도 채널별 1,800/1,800 삽입 성공, 삽입 실패, queue overrun 및
push failure는 0이었다. client 공통 group 2,545개에서도 gap과 불일치는 0이었다.

#### 판정, 원시 자료 및 원복

- SEI는 의도적인 server buffering을 추가하지 않는다.
- frame별 평균 0.087ms, 최대 1.327ms의 삽입시간은 15fps frame period
  66.667ms보다 충분히 작다.
- server CPU 증가는 약 +2.0%p로 작지만 측정 가능한 수준이며 network 증가는
  0.1Mbps 미만이었다.
- 고정 memory와 온도 증가는 역순 시험에서 재현되지 않았다.
- 종합 분석:
  `tmp/target-192.168.214.4/rtsp_sei_resource_combined_analysis.txt`
- 원시 자료:
  - `tmp/target-192.168.214.4/rtsp_sei_resource_ab_30min_20260813/`
  - `tmp/target-192.168.214.4/rtsp_sei_resource_ab_reverse_10min_20260813/`
  - `tmp/target-192.168.214.4/rtsp_sei_insert_timing_20260813/`
- 모든 실행 후 운영 binary/process checksum
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`,
  `cam-operate.service=active`, video FD 4개, 시험 환경변수 0개로 복구했다.

### 3.29 RTSP 동기화 설정 OFF/JSON/CLI/trace/stall 타겟 검증

#### 목적, 환경 및 안전 절차

2026-08-13 18:02~18:09 KST에 server `192.168.214.4`, 외부 client
`192.168.214.8`에서 새 설정 경로의 기본 OFF, 잘못된 값 정규화, JSON/CLI
우선순위, 조건부 trace와 시험 전용 stall을 순서대로 검증했다.

- 운영 server file/process SHA-256:
  `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
- 시험 server SHA-256:
  `341d29ff88d9a876722ed77cf4ced153a29225b93fda9cd8973dfff87eaf1e67`
- Frame ID client SHA-256:
  `99bbc589fc20d0a8e18ef16b0dd1401f94ee428c083b7b0cb1ef515b7dfd2515`
- decoder client 로컬 검증 SHA-256:
  `a7402bd87ecf745b6203ce2de85b75924e56b19431384422f4235abbe7938ad9`
- 원본 `/root/shared_v/edgeconf_pim.json` SHA-256:
  `e4787e7bfa8e41ce950e27a3b52ea30197eb512d40a0542f5b998c5ce92616c6`

운영 `/usr/local/bin/gstApp`는 덮어쓰지 않았다. 시험 binary는 각각
`/tmp/gstApp.sync-config-test`와
`/tmp/rtspFrameSyncClient.sync-config-test`에만 복사하고 실행 전 로컬/원격
SHA 일치를 확인했다. 운영 상태와 PID/start-time/process SHA/cmdline hash를
기록한 뒤 EXIT trap을 설치하고 `cam-operate.service`를 중지했다. 각 수동
process도 PID, start-time, executable SHA와 cmdline SHA를 기록해 그 identity가
모두 일치할 때만 종료했다.

주요 server 실행 명령은 다음과 같다. client는 필요한 단계마다
`--host 192.168.214.4 --port 8554 --user user --password user`로 네 mount에
동시에 접속했다.

```bash
/tmp/gstApp.sync-config-test -d 22 -m 4

/tmp/gstApp.sync-config-test -d 22 -m 4 --enc=h264 \
  --rtsp-frame-id-sei=1 \
  --v4l2-sync-trace-sec=-1 \
  --channel-sync-trace-sec=3601 \
  --rtsp-sync-trace-sec=3601 \
  --rtsp-test-stall-ch=4 \
  --rtsp-test-stall-after-sec=0 \
  --rtsp-test-stall-duration-sec=10

/tmp/gstApp.sync-config-test -d 22 -m 4 \
  --rtsp-frame-id-sei=0

/tmp/gstApp.sync-config-test -d 22 -m 4 \
  --v4l2-sync-trace-sec=20 \
  --channel-sync-trace-sec=20 \
  --rtsp-sync-trace-sec=20 \
  --rtsp-frame-id-sei=1

/tmp/gstApp.sync-config-test -d 22 -m 4 \
  --rtsp-frame-id-sei=1 \
  --rtsp-test-stall-ch=2 \
  --rtsp-test-stall-after-sec=7 \
  --rtsp-test-stall-duration-sec=10

/tmp/gstApp.sync-config-test -d 22 -m 4 --test=1 \
  --rtsp-frame-id-sei=1 \
  --rtsp-test-stall-ch=2 \
  --rtsp-test-stall-after-sec=7 \
  --rtsp-test-stall-duration-sec=10
```

JSON은 run-ID 전용 backup을 만든 뒤 다음과 같이 atomic 교체했고 CLI OFF
검증 직후 원본 SHA로 즉시 복구했다.

```bash
jq '.VHL_CAM.rtsp_tune.frame_id_sei="yes"' \
  /root/shared_v/edgeconf_pim.json \
  > /root/shared_v/edgeconf_pim.json.sync.tmp
mv /root/shared_v/edgeconf_pim.json.sync.tmp \
  /root/shared_v/edgeconf_pim.json

jq '.VHL_CAM.rtsp_tune.frame_id_sei=true' \
  /root/shared_v/edgeconf_pim.json \
  > /root/shared_v/edgeconf_pim.json.sync.tmp
mv /root/shared_v/edgeconf_pim.json.sync.tmp \
  /root/shared_v/edgeconf_pim.json
```

#### 직접 측정한 사실

1. **기본 OFF, 60초**
   - effective log는 `frame_id_sei:0`, 세 trace 주기 0, stall channel -1과
     시간 0이었다.
   - 네 channel이 각각 압축 AU 663/661/663/661개를 수신했지만 Frame ID가
     없어서 `barrier_ready=0`, group 0, client RC=2였다. 이는 OFF 조건의 예상
     결과다.
   - sync/SEI/stall probe 활성화 log, Frame ID barrier, `appsrc push failed`는
     없었고 server PID, video FD 4개와 RTSP 8554 listener는 측정 끝까지
     살아 있었다.

2. **잘못된 CLI와 H.264 비호환 정규화**
   - H.264+SEI, V4L2 trace -1, channel/RTSP trace 3601, 잘못된 stall 조합에
     대해 각 범주별 warning이 정확히 1회씩 발생했다.
   - effective 값은 SEI 0, 세 trace 0, stall `-1/0/0`이었다. trace/stall
     probe와 callback은 설치되지 않았다.

3. **잘못된 JSON, JSON ON, CLI OFF 우선순위**
   - 문자열 `"yes"`에서는 `frame_id_sei must be boolean or integer 0/1`과
     `1 config error(s)`가 각각 1회였고 effective SEI는 0이었다.
   - JSON `true`에서는 60초 client가 barrier ID 3을 세우고 공통 ID
     3..653의 **651 group**을 만들었다. 네 channel gap/duplicate/invalid
     SEI, alignment drop 및 `group_mismatches`는 모두 0이었다.
   - 같은 JSON `true`에서 CLI `--rtsp-frame-id-sei=0`을 주면 effective SEI가
     0으로 바뀌었다. 60초 동안 네 channel이 압축 AU 653/656/655/654개를
     받았지만 barrier와 group은 0이어서 CLI가 JSON보다 우선함을 확인했다.

4. **20초 trace와 조건부 callback**
   - V4L2 summary 2개, channel-stage summary 32개, RTSP bridge summary 4개가
     생성됐다. V4L2 두 CSI는 각각 300 frame, lost/reset/backward 0이었다.
   - 네 RTSP bridge는 channel당 300회의 push와 SEI 삽입을 모두 성공했고
     `factory_queue_overrun`, `push_fail`, `frame_id_sei_failed`는 모두 0이었다.
   - `rtsp_out_queue overrun callback connected`는 이 단계에서만 channel별
     1회, 총 4회 보였다. 나머지 단계에서는 0회였다.
   - client는 ID 3..426의 424 group을 만들었고 gap/duplicate/invalid SEI,
     alignment drop과 group mismatch는 모두 0이었다.

5. **시험 모드 전용 stall**
   - `--test=1` 없이 동일 stall 인자를 주면 warning 1회 뒤 effective
     stall이 `-1/0/0`으로 정규화됐다. 30초 client는 ID 4..215의 212 group,
     모든 channel gap과 mismatch 0이었고 stall start/end log도 없었다.
   - `--test=1`에서는 ch2에만 7초 뒤 10초 stall start/end가 각각 1회
     발생했다. client는 ch2에서 ID 106..252의 gap 147개를 관찰했고 다른
     channel의 자체 gap은 0이었다.
   - 정체 해제 뒤 공통 group이 176개 더 생성되어 최종 ID 428까지
     복구됐다. 전체 공통 group은 279개, `group_mismatches=0`이므로 서로 다른
     Frame ID를 한 group으로 묶은 사례는 없었다.

6. **종료 시점 `GST_FLOW_FLUSHING` 기록 보완**
   - **직접 측정:** JSON ON server log의 ch3에서 1회(line 94), CLI OFF server
     log의 ch0/ch1에서 각 1회(lines 82, 86), 합계 3회의
     `appsrc push failed: -2`가 있었다. `-2`는 `GST_FLOW_FLUSHING`이며 세 건은
     모두 해당 수동 process의 종료/NULL 전환 시각에 기록됐다.
   - **직접 측정:** 각 시험의 정상 측정 구간과 20초 trace summary에서는
     operational `push_fail`이 0이었고 server/client 생존성 실패도 없었다.
   - **해석(추론):** 위 세 건은 실행 중 전송 장애가 아니라 종료 과정에서
     downstream이 flushing 상태로 먼저 바뀐 뒤 남은 producer callback이
     push한 teardown race로 판단한다. 이 해석은 로그 시각과 상태 전환 순서에
     근거한 추론이며, 정상 운용 중 push failure와 구분해 기록한다.

#### 해석과 판정 한계

- **해석:** 기본값에서는 기존 RTSP 동작을 유지하며 모든 선택 probe가
  비활성이다. 올바른 JSON boolean은 SEI를 켜고 명시적 CLI 0은 이를 끈다.
- **해석:** trace가 켜진 경우에만 RTSP overrun callback이 연결되므로 기본
  경로에 불필요한 signal hookup을 추가하지 않는다.
- **해석:** 시험 stall은 test mode에 한정됐고 ch2의 의도적 147-frame 폐기 뒤
  barrier가 최신 공통 ID로 계속 진행했다.
- **한계:** 이 절은 4채널 FHD 15fps에서 최대 60초인 smoke 결과다. 장시간
  memory plateau, 30fps/high-load margin 및 실제 화면의 decoder 복구 시간은
  별도 장기 시험 범위다.

#### 원시 자료와 원복

- 원시 자료:
  `tmp/target-192.168.214.4/rtsp_sync_config_20260813_180234/`
- harness 자체 assertion 결과: `STATUS=PASS`, 여덟 단계 모두 완료
- 원복 직후와 별도 재확인 결과:
  - `cam-operate.service=active`
  - 운영 PID `239348`
  - 운영 file/process SHA가 모두
    `3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`
  - 명령 `gstApp -d 22 -m 4`
  - video FD 4개, RTSP 8554 listening
  - 수동 시험 process 0, 임시 config backup 0
  - 원본 config SHA
    `e4787e7bfa8e41ce950e27a3b52ea30197eb512d40a0542f5b998c5ce92616c6`
  - server/client 시험 binary와 run-ID 원격 자료 0개

운영 service를 복구하고 위 항목을 확인한 뒤에만 원격 원시 자료와 시험
binary를 삭제했다. 삭제 전 모든 raw log는 위 로컬 경로에 복사했다.


### 3.30 최종 검토 결함 보완과 RTSP 세대/decoder/종료 검증

#### 목적과 구현 범위

3.29의 설정 시험 뒤 최종 코드 검토에서 발견된 Critical 2건, Important 6건,
Minor 3건을 한 번에 보완했다. 이 절은 각 결함의 코드/결정적 시험 근거와
타겟에서 직접 측정한 결과를 구분해 기록한다.

1. **SEI 대상과 입력 계약**
   - 공통 sample handler와 삽입 helper 모두 video channel
     `0..MAX_CHANNEL-1`, H.265, 고정 caps
     `video/x-h265,stream-format=byte-stream,alignment=au`, 양의 framerate와
     완전한 Annex-B AU를 확인한 뒤에만 SEI parser를 호출한다.
   - single/dual encoder의 RTSP 경로 모두 `h265parse` 뒤 strict capsfilter를
     지나 appsink에 도달한다. audio ch4는 MPEG 경로로만 전달되며 SEI 대상이
     아니다.
   - shared `AudioBin`은 video loop 뒤 한 번만 초기화하고 recording mux와
     RTSP ch4로 fan-out한다. RTSP server는 mount 등록 전에 한 번만 시작하며,
     record-only audio에서는 ch4 RTSP mount를 만들지 않는다.

2. **Frame ID rate**
   - payload Frame ID는 설정 배열이 아니라 sample의 실제 negotiated caps
     framerate와 PTS에서 계산한다.
   - single encoder에서는 effective RTSP fps를 record encoder fps와 맞추고,
     dual encoder는 독립 RTSP fps를 유지한다. zero/invalid fraction과 channel
     범위 오류는 output sentinel을 바꾸지 않고 실패한다.

3. **엄격한 standalone client와 decoder 판정**
   - Frame ID client는 bounded Annex-B scanner로 모든 NAL header를 검증하고,
     prefix/suffix SEI type, payload type 4, size 30, T.35 `ff/c1`, magic
     `GSTSYNC1`, version/channel/reserved, big-endian 64-bit 값과 정확한 RBSP
     trailing bits를 확인한다. 임의 byte 검색은 제거했다.
   - duplicate/backward ID는 queue/last-ID 갱신 전에 거부한다. 기본 모드는
     missing/invalid/duplicate/backward/gap/alignment와 ERROR/EOS를 모두
     실패로 판정한다. OFF, 의도적 channel gap, reconnect/startup loss는
     충돌 불가능한 명시 option에서만 허용한다.
   - decoder client는 ERROR/EOS를 fatal로 추적하고, threshold 이상의 **매
     qualifying gap**마다 이전 recovery 증거를 초기화한다. PASS에는 gap 전
     decoded sample, 주입 gap, 그 뒤 IDR, 그 IDR 이후 decoded sample이 모두
     필요하다.

4. **media generation과 종료**
   - media configure마다 refcounted generation을 새로 만들고 trace/probe/pad/
     signal hook를 소유하게 했다. callback은 generation ref를 들고 inactive
     또는 과거 generation을 거부한다. unprepare는 먼저 deactivate한 뒤
     일치하는 generation만 detach하고 hook를 lock 밖에서 제거한다.
   - test stall은 20ms 이하 slice로 interruption/generation active 상태를
     확인하고 완료 또는 취소 뒤 `GST_PAD_PROBE_REMOVE`를 반환한다.
   - 종료 시 listener source ID를 먼저 제거하고 기존 RTSP client를
     `GST_RTSP_FILTER_REMOVE`로 닫은 뒤 timer, mount와 server ref를 해제한다.
     이 순서는 종료 도중 새 media가 생성되거나 `gst_deinit()`이 남은 RTSP
     task를 기다리는 상황을 막는다.

5. **설정과 trace 의미**
   - `cfg_get_bool()`은 `json_object_object_get_ex()`로 absent와 explicit null을
     구분한다. null은 `CFG_BOOL_BAD_TYPE`이며 caller output은 그대로다.
   - trace용 `origin_index`와 표준 timestamp meta duration의 ordinal 저장을
     제거했다. duration은 `GST_CLOCK_TIME_NONE`, timestamp는 원본 PTS만 담는다.
   - 3.29의 teardown `GST_FLOW_FLUSHING` 3건과 1절의 CSI/video device mapping도
     각각 직접 측정/추론 구분과 실제 default에 맞게 보정했다.

#### 결정적 local/target test

`testRtspSync`는 caps/AU/rate/generation/cancel core를 63개 assertion으로,
`testRtspValidation`은 Annex-B/SEI/verdict/IDR/최신-gap recovery를 92개
assertion으로 검증했다. `testCfgjson`은 explicit null과 unchanged sentinel을
포함한 30개 assertion을 통과했다. source gate는 media hook 수명, strict caps,
RTSP listener/client/mount teardown, shared audio 및 server-start 순서를 확인한다.

타겟 시험 환경은 server `192.168.214.4`, client `192.168.214.8`이다. 운영
`/usr/local/bin/gstApp`를 변경하지 않고 시험 binary는 `/tmp`에만 복사해 SHA를
확인했다. 전체 기능 실행 `20260813_230702`의 시험 server SHA는
`050f294df8b705126d81134759967db1b5fec7b163c5c880e8b9edc5ec72ed93`,
최종 teardown 보완 뒤 집중 실행의 server SHA는
`ce279732e7115e3677405c3ed657db2ef3534fd4ba06e46a14fd9f22637adf5b`이다.

#### 직접 측정한 사실

1. **기본/엄격 parser 판정**
   - 기본 OFF 60초에서 네 channel은 압축 AU 906/906/909/908개를 받았고
     barrier/group은 0, invalid SEI와 fatal bus는 0이었다.
   - JSON ON strict 60초에서 공통 ID 68..961의 894 group을 만들었다. 네
     channel의 operational missing, invalid, duplicate, backward, gap,
     alignment drop과 `group_mismatches`는 모두 0이었다.
   - 최종 binary 집중 실행 `20260813_235111`에서 ch1을 6회 재접속했다.
     7 epoch, 510 group을 만들었고 네 channel의 missing/invalid/duplicate/
     backward/gap은 모두 0, `fatal_bus=0`, `group_mismatches=0`이었다.

2. **H.265 + MPEG audio ch4**
   - 최종 binary 실행 `20260813_235446`에서 ch4
     `rtpmpadepay ! mpegaudioparse` client가 정상 종료했고 `media connect ch:4`를
     확인했다. server에서 Frame ID enable log는 video ch0~ch3 네 건뿐이며
     ch4 삽입 시도/실패는 0이었다.
   - video 네 generation의 20초 summary는 channel당 300 frame, SEI
     inserted/failed 300/0, push failure 0이었다. strict client는 ID 85..907의
     823 group, 모든 channel missing/invalid/gap/duplicate/backward 0,
     mismatch 0이었다.

3. **서로 다른 single-encoder 설정 fps**
   - `frec=30, frtsp=15`에서 35초 동안 ID 140..1179의 1,040 group,
     `frec=15, frtsp=30`에서 ID 70..599의 530 group을 만들었다.
   - 두 방향 모두 네 channel의 operational gap, duplicate, backward,
     missing/invalid SEI와 group mismatch가 0이었다. 따라서 실제 negotiated
     encoder rate와 Frame ID 증가율이 일치했다.

4. **generation reconnect와 decoder recovery**
   - 20초 RTSP trace 도중 ch2를 2초 끊은 실행에서 generation 1 deactivate,
     generation 2 enable과 새 300-frame summary를 각각 확인했다. client는 두
     epoch에서 941 group, reconnect 1/1, mismatch 0이었다.
   - decoder 시험은 server가 ch2를 4초 정체한 start/end log를 각각 1회 남겼다.
     client가 측정한 qualifying gap은 4,042.145ms였다. gap 전 decode, gap 뒤
     recovery IDR 21개와 그 IDR 이후 decode를 확인해 `verdict=PASS`였으며
     fatal/EOS는 0이었다.

5. **3600초 stall 취소와 process 종료**
   - 최종 binary 실행 `20260813_234804`에서 SIGTERM log
     `14:49:03.636`, stall cancel `14:49:03.646`으로 취소 표식은 약 10ms 뒤
     나타났다. pipeline unref는 `14:49:03.978`이었다.
   - identity/start-time을 고정한 `/proc` 100ms poll에서 PID는 4번째 poll 안에
     사라져 `EXITED=1`이었다. 한 시간을 기다리거나 watchdog KILL을 사용하지
     않았다.

#### 해석, 한계와 원복

- **직접 측정:** 위 숫자는 각 raw log의 summary와 harness assertion에서 읽은
  값이다. 마지막 세 집중 실행은 최종 teardown 보완 binary SHA로 수행했다.
- **해석(추론):** 전체 기능 실행 뒤 추가된 listener/client/mount 정리는
  RTSP stop 수명에만 작용한다. 그 뒤 최종 binary로 strict 6회 reconnect,
  H.265+audio와 bounded shutdown을 다시 통과했으므로 앞선 fps/decoder 측정의
  데이터 경로 결론을 바꾸지 않는다고 판단한다.
- **한계:** 시험은 15/30fps에서 phase당 약 20~65초인 bounded 검증이다. 수시간
  memory plateau, 여러 channel 동시 장애와 실제 표시 화면의 복구 latency는
  별도 장기 검증 대상이다.

원시 자료는 다음 경로에 보존했다.

- 전체 기능 phase(OFF/JSON/trace/stall/audio/fps/reconnect/decoder):
  `tmp/target-192.168.214.4/rtsp_sync_final_fix_20260813_230702/`
- 최종 binary bounded shutdown PASS:
  `tmp/target-192.168.214.4/rtsp_sync_final_fix_20260813_234804/`
- 최종 binary strict 6회 reconnect:
  `tmp/target-192.168.214.4/rtsp_sync_final_fix_20260813_235111/`
- 최종 binary H.265+audio:
  `tmp/target-192.168.214.4/rtsp_sync_final_fix_20260813_235446/`

마지막 실행 뒤 직접 확인한 원복 상태는 service `active`, 운영 file/process SHA
`3bf7329ff5cd5f3a1fa536291bd3ec192d1c2cf113289c5488d31c0e27f7566b`,
config SHA
`e4787e7bfa8e41ce950e27a3b52ea30197eb512d40a0542f5b998c5ce92616c6`,
명령 `gstApp -d 22 -m 4`, video FD 정확히 4개, 8554 listen, 수동 server/frame/
decoder process 0, 임시 backup/원격 시험 artifact 0이다. 원복 뒤 운영 네
channel Annex-B flow도 다시 확인했다.

## 4. 현재까지의 결론

2026-08-13 현재 다음 사항은 측정으로 확인됐다.

1. 네 AP1302 출력은 동일한 15fps 증가량을 보였다.
2. AP1302 입력 FV와 출력 HINF 사이에 지속적인 프레임 누락이 없었다.
3. CSI0과 CSI1의 CSI interrupt 증가량이 같았다.
4. 안정 구간에서 두 V4L2 stream은 동일하고 연속적인 sequence 범위를 전달했다.
5. CSI 간 V4L2 PTS 차이는 대부분 1ms 이내였고 1프레임 오프셋은 없었다.
6. 네 채널의 branch 입력과 crop 출력은 각각 900프레임, 동일 sequence 범위로
   누락 없이 전달됐다.
7. 네 encoder 입력은 각각 900프레임이며 동일한 15fps PTS timeline을 가졌다.
8. `videorate`의 drop/duplicate 수는 pipeline 기동 위상과 입력 PTS jitter에
   따라 실행별로 달랐지만, lineage 실행에서 네 채널의 동작은 동일했다.
9. `videorate` 출력부터 encoder 출력과 record/RTSP tee 분기까지 모두 900개였고,
   네 채널과 모든 지점의 PTS 목록이 완전히 같았다.
10. 원본 sequence lineage 결과 네 채널 모두 sequence 490을 제외하고 489와
    493을 중복했으며, 900개 출력의 원본 sequence 순서도 완전히 같았다.
11. RTSP bridge에서 채널당 원본 AU 900개가 모두 appsrc로 push됐고, bridge,
    appsrc, payloader 및 RTP 출력에서 추가 프레임 손실은 관찰되지 않았다.
12. RTSP의 기존 upstream PTS는 의도대로 제거되고 각 media pipeline의 새 PTS와
    독립 RTP timestamp가 부여됐다. 따라서 RTSP timestamp만으로 채널 간 같은
    원본 프레임을 식별할 수 없다.
13. 네 mount 동시 접속 시험에서 appsrc 준비 시점 차이로 client 제공 시작
    원본 프레임이 ch0~ch3 사이 최대 3프레임 달랐다. 이는 capture 동기 불량이
    아니라 독립 RTSP session 시작 정책의 결과다.
14. RTCP SR 81개의 NTP↔RTP 관계는 채널 내부에서 0.03ms 이내로 안정적이었고,
    독립 RTP timestamp를 공통 NTP 축으로 변환할 수 있었다.
15. 같은 실행의 upstream PTS lineage와 결합한 결과 동일 원본 프레임의 환산
    RTSP 송출 시각 spread는 시작부 4.376ms, 약 60초 지점 18.223ms였다. 이는
    각각 0.066프레임과 0.273프레임이며 1프레임 오차는 관찰되지 않았다.
16. H.265 SEI로 공통 frame ID와 원본 PTS를 전달한 결과 client capture에서
    네 채널 공통 ID 84..1201의 연속된 1,118개를 직접 식별했고, 같은 ID의
    원본 PTS 불일치는 0이었다.
17. 공통 ID별 RTCP NTP 송출 시간 spread는 중앙값 12.662ms, P99 23.637ms,
    최대 62.012ms였다. 1,118개 모두 한 프레임 66.667ms 이내였다.
18. standalone RTSP client가 시작 시 최대 3 frame의 channel 차이를 공통 ID
    barrier로 흡수했고, ID 52..503의 연속 452 group을 gap, duplicate,
    invalid SEI 및 운용 중 alignment drop 없이 생성했다.
19. ch2를 3초 disconnect한 시험에서 46개 ID의 의도적 공백을 구분하고 ID
    214에서 barrier를 재수립했다. 재접속 후 ID 214..498의 연속 285 group에서
    비의도적 gap, duplicate, SEI 오류 및 alignment drop은 모두 0이었다.
20. 기존 `rtspsrc` element의 NULL→PLAYING 재사용은 dynamic pad `not-linked`로
    실패했으며, channel pipeline 전체 재생성이 정상 복구 방식임을 확인했다.
21. ch2 pipeline을 3회 반복 재생성한 시험에서 네 epoch의 barrier가 모두
    성립했고 총 438개 공통 group을 생성했다. ch2 의도적 skip 90개를 제외한
    gap, duplicate, SEI 오류, group 불일치 및 alignment drop은 모두 0이었다.
22. client appsink callback만 15초/30초 지연하면 loopback TCP와 client queue가
    과거 frame을 적재했다. server drop은 없었고 지연 해제 뒤 적체 frame이
    빠르게 재생됐으므로 callback 지연만으로 TCP backpressure를 증명할 수 없다.
23. ch2 payloader downstream을 10초 정체한 시험에서 factory queue overrun
    147회와 ch2 Frame ID gap 147개가 정확히 일치했다. 다른 세 channel의 자체
    gap은 0이었다.
24. 정체 해제 뒤 ID 305..655의 공통 group 351개가 자동으로 재개됐고 전체
    group 불일치는 0이었다. 제한된 leaky queue가 과거 frame을 폐기하고 최신
    구간으로 복귀하는 동작을 확인했다.
25. 전용 외부 client `192.168.214.8` baseline에서 공통 Frame ID 444개를 gap,
    duplicate, invalid SEI와 group mismatch 없이 생성했다. 최대 도착 spread는
    38.826ms로 15fps 한 frame 이내였다.
26. 이전 host 외부망 invalid SEI는 parser 호환성 문제가 아니라 SEI 계측 코드가
    없는 원본 운영 binary로 시험한 설정 오류였다. 계측 binary 재시험에서는
    x86_64/ARM64 외부 client 모두 invalid SEI 0이었다.
27. 외부망 TCP packet loss 0.1%/0.5%/1.0%에서 kernel packet은 각각
    16/73/151개 폐기됐지만 TCP 재전송으로 Frame ID gap은 모두 0이었다. 다만
    1%에서 동일 frame 최대 도착 spread는 424.967ms까지 증가했다.
28. 정상 총 전송량 약 15Mbps를 12Mbps로 제한하면 server factory queue
    overrun과 client gap이 ch0/ch1/ch2/ch3=14/6/4/0으로 정확히 일치했다.
29. 8Mbps 제한에서는 channel gap 107개, alignment drop 181개와 최대 도착
    spread 18.941초가 발생했다. barrier는 group mismatch 없이 동작했지만
    시간 기반 queue timeout/불완전 group 폐기 정책이 필요함을 확인했다.
30. SEI OFF/ON을 30분과 역순 10분으로 비교한 결과 process CPU 증가는
    +1.925~+2.020%p로 재현됐고 system CPU 증가는 +0.518~+0.630%p였다.
31. PSS 평균과 온도 차이는 시험 순서를 뒤집었을 때 방향이 바뀌어 SEI의 고정
    memory 또는 온도 증가로 재현되지 않았다. 다만 수시간 memory plateau는
    아직 확인하지 않았다.
32. SEI 삽입 7,200회의 수행시간은 평균 86.877us, 99.35%가 250us 이하였고,
    최대 1.326793ms였다. 삽입 실패와 server drop은 0이었다.
33. 30분 외부 client에서 공통 ID 27,298개를 gap/duplicate/invalid/mismatch 없이
    생성했다. 평균 도착 spread는 12.589ms였으나 최대는 89.939ms로 한 frame을
    넘었다.
34. 새 RTSP 동기화 설정은 기본 OFF, strict JSON boolean, CLI 우선순위와
    test-mode-only stall을 타겟에서 충족했다. 20초 trace에서는 세 summary
    범주와 channel별 callback만 조건부로 활성화됐고, ch2 10초 stall 뒤에도
    잘못된 Frame ID group 없이 공통 barrier가 재개됐다.
35. bounded H.265 parser와 엄격 verdict를 적용한 client가 JSON ON 894 group 및
    6회 반복 재접속 510 group을 missing/invalid/duplicate/backward/gap/mismatch
    없이 검증했다.
36. H.265 video 네 channel과 MPEG audio ch4를 함께 실행해 video SEI 1,200회,
    ch4 MPEG flow, strict 공통 ID 823 group을 확인했고 ch4 SEI 접근은 0이었다.
37. single encoder의 `frec/frtsp`를 30/15와 15/30으로 서로 다르게 설정해도
    negotiated rate 기반 Frame ID는 각각 1,040/530 group에서 duplicate와
    gap을 만들지 않았다.
38. media별 trace generation은 reconnect에서 구분됐고 decoder는 주입한
    4.042초 gap 뒤 IDR/decoded output까지 확인했다. 3600초 시험 stall은
    SIGTERM 약 10ms 뒤 취소됐고 process는 100ms poll 네 번 안에 종료됐다.
39. 최종 모든 타겟 실행 뒤 운영 binary/config/process SHA, 명령, video FD 4개,
    RTSP port와 실제 네 channel flow를 원본 상태로 복구했다.

아직 증명되지 않은 사항은 다음과 같다.

1. 네 센서 노출 시작 위상이 실제로 sub-ms 단위에서 같은지 여부
2. 고부하 또는 수시간 운용 중 drop, 재정렬 시간과 queue/memory plateau
3. 기본 비활성 상태인 H.265 SEI frame ID prototype과 검증 client의 barrier를
   제품 server/client 기능으로 적용할지 여부
4. 장시간 정상망에서 드물게 한 frame을 넘은 도착 spread를 제품 client의
   buffering/timeout 정책으로 제한했을 때 화면 지연과 폐기율
5. 단일 주입 gap의 decoded-buffer 복구는 확인했지만, 여러 channel 동시 장애,
   반복/장시간 장애와 실제 표시 화면이 다음 IDR에서 정상화되는 시간

3번은 H.265 SEI prototype과 standalone client로 기술적 해결 가능성을 확인했다.
제품 적용 여부, payload 식별자 할당, 장애/재접속 정책 및 H.264 지원 범위는
별도 정책 결정이 필요하다.

## 5. 다음 검증 단계

공통-ID 전달과 시작 barrier의 기본 동작까지 확인했으므로 다음 우선순위는
장애 조건과 제품 정책 검증이다.

1. CPU/VPU 부하를 가해 `factory_queue_overrun`, `full_drop`, `key_wait_drop`,
   `push_fail`, 재정렬 시간과 memory 상한을 검증한다.
2. decoder client로 queue 폐기 직후 첫 정상 화면/IDR까지의 복구 시간을
   측정한다.
3. 여러 channel 동시 장애에서 pipeline 재생성과 epoch 전환이 정상 동작하는지
   확인한다.
4. 장시간 외부망 시험에서 동일-ID 도착 spread, queue depth와 memory 상한을
   측정해 제품 client의 buffering 시간 및 channel timeout을 결정한다.
5. 제품 적용 시 SEI payload 식별자, version 호환, H.264 지원, 인증 실패 및
   일부 channel 미접속 시 동작을 명세한다.
6. 센서 노출 위상 자체를 증명해야 한다면 AP1302 register/metadata 또는 광학
   자극을 사용한 별도 물리 계층 시험을 수행한다. 현재 Frame ID는 V4L2 이후
   동일 lineage의 증거이지 센서 노출 시각의 직접 증거는 아니다.
7. 15fps SEI OFF/ON A/B는 완료했다. 4채널 30fps stress와 수시간 memory
   plateau 시험으로 제품 운용 margin을 추가 확인한다.
8. Windows FFmpeg client에서 software decode와 D3D11VA를 분리해 CPU/GPU,
   compressed queue memory, 동일-ID 대기시간 및 render 완료시간을 측정한다.

## 6. 앞으로의 기록 원칙

제품 RTSP client 구현 요구사항은
`docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md`에 별도로 정리한다.
Windows client library 선택, 제품 server 변경점과 delay/resource 영향 분석도
같은 문서의 16~19절에 기록한다.

후속 시험은 모두 다음 원칙으로 이 문서에 반영한다.

1. **시험 및 결과** 아래에 번호가 있는 새 절을 추가한다.
2. 타겟, 바이너리 checksum, 정확한 JSON/CLI 설정과 측정 시간을 기록한다.
3. 원시 로그를 `tmp/target-192.168.214.4/` 아래에 보존하고 문서에 경로를 남긴다.
4. 직접 관찰한 사실과 해석을 분리하고 판정 한계를 명시한다.
5. 제외한 시작/warm-up 표본 수와 제외 이유를 기록한다.
6. 임시 측정은 `/tmp` 바이너리를 수동 PID로 실행하고 종료한 뒤 운영 service를
   재시작한다. 운영 process 명령이 `gstApp -d 22 -m 4`인지, 수동 PID가 남지
   않았는지, video-device FD와 녹화 시작 marker가 정상인지 확인한다.
7. **현재까지의 결론**과 **다음 검증 단계**를 함께 갱신한다.
8. 문서는 한글로 작성하되 레지스터명, 현재 CLI/JSON, 과거 측정 환경변수,
   코드 식별자 및 파일 경로는 정확성을 위해 원문 표기를 유지한다.
