# 저장 영상 QP 통계 수집 검토

## 문서 상태

- 상태: 향후 구현 검토용
- 작성일: 2026-08-06
- 적용 코덱: H.264, H.265
- 현재 gstApp 구현 여부: 미구현

## 목적

저장 영상의 프레임별 QP(Quantization Parameter)를 로그 또는 별도 파일로
남기고, 녹화 파일 단위로 다음 통계를 제공하는 방안을 정리한다.

- 프레임별 QP
- 전체 프레임 수
- 최소 QP
- 최대 QP
- 평균 QP

## QP 값의 정의

구현 전에 어떤 값을 "프레임 QP"로 사용할지 명확히 해야 한다.

1. **Picture/Header QP (`qpHdr`)**
   - 프레임마다 하나의 값을 수집한다.
   - 녹화 파일 전체의 최소/최대/평균 계산이 간단하다.
   - 현재 VPU wrapper의 디버그 로그에서 확인 가능한 값이다.
   - Rate Control 설정에서 조회한 값이므로, 프레임 내부 모든 CTU/매크로블록에
     최종 적용된 QP의 평균과는 다를 수 있다.

2. **프레임 내부 블록 QP 통계**
   - CTU/매크로블록별 실제 QP를 기준으로 프레임 내부의 최소/최대/평균을 구한다.
   - 현재 공개된 VPU wrapper 출력과 GStreamer buffer에서는 얻을 수 없다.
   - VPU 하드웨어 출력 통계 지원 여부를 추가로 확인하거나, 비트스트림을 깊게
     분석해야 한다.

초기 구현의 권장 기준은 프레임별 `qpHdr`이며, 결과 파일에 반드시
`qp_type: picture_header`처럼 값의 의미를 기록한다.

## 현재 코드에서 확인된 내용

### VPU wrapper

`imx-vpuwrap/vpu_wrapper_hantro_VCencoder.c`는 인코딩 호출 직전에
`VCEncGetRateCtrl()`로 설정을 읽고 다음 값을 디버그 로그로 출력한다.

```text
rcCfg.qpHdr <value>
rcCfg.bitPerSecond <value>
```

관련 위치:

- `/home/jhw/ai/opencode/projects/imx-vpu/imx-vpuwrap/vpu_wrapper_hantro_VCencoder.c`
  - `VPU_ENC_LOG("rcCfg.qpHdr %d", rcCfg.qpHdr)`
- `/home/jhw/ai/opencode/projects/imx-vpu/imx-vpuwrap/vpu_wrapper.h`
  - `VpuEncEncParam` 출력 필드에는 실제 프레임 QP가 정의되어 있지 않다.

### GStreamer VPU encoder plugin

`imx-gst1.0-plugin/plugins/vpu/gstvpuenc.c`는
`VPU_EncEncodeFrame()` 결과로 압축 데이터를 만들지만 QP를 `GstMeta`, tag 또는
signal로 전달하지 않는다. 따라서 현재 gstApp에서 encoder 출력 pad에 probe만
추가해도 QP를 직접 읽을 수 없다.

### 녹화 파일 경계

gstApp은 `main.cpp`에서 다음 splitmuxsink 메시지를 이미 처리한다.

- `splitmuxsink-fragment-opened`
- `splitmuxsink-fragment-closed`

이 메시지는 `MuxSinkBin::handleFragmentOpened()`와
`MuxSinkBin::handleFragmentClosed()`로 전달된다. 향후 녹화 파일별 QP 통계를
초기화하고 최종 저장하는 기준으로 활용할 수 있다.

## 임시 진단 방법

제품 기능을 추가하지 않고 VPU wrapper의 디버그 로그로 `qpHdr` 변화를 확인할
수 있다. `/etc/vpu_log_level`은 VPU가 로드되기 전에 설정해야 한다.

```sh
echo 1 > /etc/vpu_log_level
./gstApp <arguments> 2>&1 | tee /tmp/vpu_qp.log
```

프레임별 QP를 CSV로 추출한다.

```sh
grep -o 'rcCfg.qpHdr [0-9]\+' /tmp/vpu_qp.log |
awk 'BEGIN { print "frame,qp" } { print NR-1 "," $2 }' \
    > /tmp/frame_qp.csv
```

전체 프레임의 최소/최대/평균을 계산한다.

```sh
grep -o 'rcCfg.qpHdr [0-9]\+' /tmp/vpu_qp.log |
awk '
{
    q = $2
    if (count == 0 || q < min) min = q
    if (count == 0 || q > max) max = q
    sum += q
    count++
}
END {
    if (count > 0)
        printf("frames=%d min=%d max=%d avg=%.2f\n",
               count, min, max, sum / count)
}'
```

진단이 끝난 후 wrapper 로그를 끄려면 다음 값을 설정하고 애플리케이션을 다시
시작한다.

```sh
echo 0 > /etc/vpu_log_level
```

### 임시 방법의 한계

- `vpu_log_level` bit 0은 QP 전용이 아니라 wrapper API 로그 전체를 활성화하므로
  로그량이 많다.
- 여러 encoder가 동작하면 출력이 하나의 로그에 섞인다.
- 현재 로그에는 gstApp 채널 번호나 녹화 파일명이 없어서 dual 채널 결과를
  신뢰성 있게 분리할 수 없다.
- 출력 시점이 실제 인코딩 전에 조회한 Rate Control 값이므로 최종 블록별 QP를
  의미하지 않는다.
- 진단용으로만 사용하고 상시 운영 기능으로 사용하지 않는다.

## 권장 구현 구조

```text
VPU encoder
    -> frame QP/statistics output
    -> GStreamer vpuenc
    -> channel-aware collector
    -> splitmuxsink fragment mapping
    -> per-frame CSV + per-file summary JSON/log
```

### 1. VPU wrapper 출력 확장

- vendor `VCEncOut` 또는 관련 API가 실제 picture QP나 QP 합계를 제공하는지 먼저
  확인한다.
- 제공되는 값이 있으면 명시적인 frame statistics 구조체로 wrapper API에
  전달한다.
- `nReserved[]`를 임시로 재사용하지 않는다. ABI 변경 범위와 기존 호출자 영향을
  확인한 뒤 이름 있는 필드를 추가한다.
- H.264와 H.265에서 값의 의미와 범위가 같은지 확인한다.

예상 정보:

```text
valid
codec
picture_type
picture_qp
pts
encoded_size
```

### 2. GStreamer plugin 전달

- `gstvpuenc.c`에서 인코딩이 끝난 output buffer와 통계를 연결한다.
- custom `GstMeta` 또는 plugin 내부 signal/callback을 사용할 수 있다.
- parser가 buffer를 교체하면서 custom metadata를 유실할 수 있으므로, gstApp은
  encoder 직후에 값을 수집하거나 metadata transform을 구현한다.

### 3. gstApp 채널별 집계

- single/shared encoder 경로는 `EncoderBin`, dual 녹화 encoder 경로는
  `RecordBin`에서 수집 지점을 둔다.
- 채널과 encoder 인스턴스가 명확히 연결된 상태에서 다음 누적값을 관리한다.

```text
count
sum_qp
min_qp
max_qp
```

평균은 fragment 종료 시 `sum_qp / count`로 계산하며, `count == 0`을 별도로
처리한다. 장시간 녹화에 대비해 합계는 충분히 큰 정수형을 사용한다.

### 4. splitmuxsink 녹화 파일 연결

- `fragment-opened`: 파일 경로와 running-time을 등록한다.
- encoder frame: PTS/running-time을 기준으로 해당 fragment에 QP를 누적한다.
- `fragment-closed`: 파일 경계를 확정하고 sidecar 파일과 요약 로그를 기록한다.
- async finalize 또는 다음 fragment가 먼저 열린 경우를 고려해 단일 전역
  accumulator가 아니라 채널별 fragment 상태를 유지한다.
- 파일 경계 부근의 프레임이 잘못 귀속되지 않도록 wall clock이 아닌
  PTS/running-time을 사용한다.

## 권장 저장 형식

영상 파일과 동일한 경로와 basename을 사용하는 sidecar 파일을 권장한다.

```text
record_20260806_090000.mp4
record_20260806_090000.qp.csv
record_20260806_090000.qp.json
```

프레임별 CSV 예시:

```csv
frame,pts_ns,picture_type,qp,encoded_size
0,0,I,24,82341
1,33333333,P,27,14231
2,66666666,P,28,11982
```

요약 JSON 예시:

```json
{
  "version": 1,
  "channel": 0,
  "codec": "h265",
  "qp_type": "picture_header",
  "video_file": "record_20260806_090000.mp4",
  "frame_count": 1800,
  "qp_min": 22,
  "qp_max": 34,
  "qp_avg": 27.42
}
```

요약 로그 예시:

```text
[GST][QP] ch0 file=record_20260806_090000.mp4 codec=h265 frames=1800 min=22 max=34 avg=27.42
```

프레임별 CSV는 파일 크기와 I/O가 증가하므로 JSON 옵션으로 다음 운영 모드를
선택할 수 있게 하는 것이 좋다.

```text
off      : 수집하지 않음
summary  : 녹화 파일별 min/max/avg만 저장
frame    : 프레임별 CSV와 요약을 모두 저장
```

기본값은 `off` 또는 운영 요구에 따라 `summary`로 정한다.

## 구현 시 검증 항목

- H.264와 H.265에서 QP 수집 및 범위가 정상인지 확인
- `qp-min`, `qp-max` 설정 범위를 벗어난 값이 기록되는지 확인
- CBR/VBR 및 고정 QP 설정별 값 변화 확인
- single/dual 채널에서 채널별 프레임 수와 통계가 섞이지 않는지 확인
- split 직전/직후 프레임이 올바른 영상 파일에 귀속되는지 확인
- 녹화 중단, EOS, 비정상 종료 시 sidecar 파일 처리 확인
- QP 수집 비활성화 시 성능과 기존 동작에 영향이 없는지 확인
- `summary`와 `frame` 모드의 CPU, 저장장치 I/O 및 파일 크기 측정

## 결론

현재 코드 수정 없이 가능한 범위는 wrapper의 `qpHdr` 디버그 로그를 이용한
진단과 사후 통계 계산이다. 채널별·녹화 파일별로 신뢰할 수 있는 QP 통계를
상시 저장하려면 VPU wrapper와 GStreamer plugin에서 QP를 명시적으로 전달하고,
gstApp에서 splitmuxsink fragment의 PTS/running-time과 연결하는 구현이 필요하다.
