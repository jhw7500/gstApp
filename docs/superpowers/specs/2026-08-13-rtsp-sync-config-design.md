# RTSP 동기화 설정 통합 설계

- 작성일: 2026-08-13
- 대상: `gstApp`
- 상태: 사용자 승인 완료

## 1. 목적

현재 환경변수로 활성화하는 Frame ID SEI, 동기화 계측 및 RTSP stall 시험
설정을 기존 애플리케이션의 `CmdArg`·JSON·CLI 설정 체계로 이관한다.

운영 기능, 진단 기능과 장애 주입 기능을 구분하여 다음 목표를 달성한다.

1. 모든 신규 설정의 기본값을 비활성으로 유지한다.
2. 운영 영상의 기존 기본 동작을 유지한다.
3. 설정 출처와 우선순위를 명확하게 한다.
4. 계측이 꺼져 있으면 pad probe와 RTSP overrun callback을 설치하지 않는다.
5. 장애 주입 기능은 일반 운영 mode에서 실행할 수 없게 한다.

## 2. 범위

### 2.1 포함

- H.265 RTSP Frame ID SEI 활성 설정의 JSON·CLI 이관
- V4L2, encoder/channel, RTSP 계측 시간의 CLI 이관
- RTSP stall 시험 설정의 test-mode 전용 CLI 이관
- RTSP overrun callback의 조건부 연결
- 환경변수 참조 제거
- 설정 검증, 시작 로그, 시험 스크립트와 관련 문서 갱신

### 2.2 제외

- H.264 Frame ID SEI 지원
- 실행 중 설정 변경 또는 동적 재구성
- JSON을 통한 계측·stall 설정
- `gstreamer-codecparsers-1.0` 의존성의 선택적 링크
- Frame ID payload version 또는 형식 변경
- Windows client 구현 변경

## 3. 설정 우선순위

기존 시작 순서를 그대로 사용한다.

```text
컴파일 기본값
  → JSON
  → CLI
  → check_arg() 검증 및 정규화
  → cmdArg 확정
```

CLI는 JSON을 최종적으로 덮어쓴다. 예를 들어 JSON에서 SEI를 활성화해도
`--rtsp-frame-id-sei=0`으로 해당 실행에서 비활성화할 수 있다.

환경변수 fallback은 제공하지 않는다. 기존 환경변수와 새 설정이 동시에 존재할
때 발생할 수 있는 우선순위 모호성을 제거하기 위함이다.

## 4. 설정 인터페이스

### 4.1 `CmdArg` 필드

`CmdArg`에 다음 필드를 추가한다.

| 필드 | 타입 | 기본값 | 용도 |
| --- | --- | ---: | --- |
| `rtsp_frame_id_sei` | `gboolean` | `FALSE` | H.265 Frame ID SEI 삽입 |
| `v4l2_sync_trace_sec` | `gint` | `0` | V4L2 입력 계측 시간 |
| `channel_sync_trace_sec` | `gint` | `0` | encoder/branch 계측 시간 |
| `rtsp_sync_trace_sec` | `gint` | `0` | RTSP bridge/RTP 계측 시간 |
| `rtsp_test_stall_ch` | `gint` | `-1` | stall 대상 channel |
| `rtsp_test_stall_after_sec` | `gint` | `0` | 시작 후 stall까지 시간 |
| `rtsp_test_stall_duration_sec` | `gint` | `0` | stall 유지 시간 |

설정은 시작 시 확정되며 pipeline 실행 중에는 변경하지 않는다.

### 4.2 JSON

운영 기능인 Frame ID SEI만 기존 `rtsp_tune` 객체에 선택 항목으로 추가한다.

```json
{
  "VHL_CAM": {
    "rtsp_tune": {
      "frame_id_sei": true
    }
  }
}
```

- key가 없으면 기본값 `false`를 유지한다.
- JSON boolean `true`/`false`를 표준 형식으로 사용한다.
- 호환성을 위해 정수 `0`/`1`도 수용한다.
- 그 외 타입이나 값은 경고하고 기본값 `false`를 적용한다.
- 기존 JSON에 새 key가 없어도 오류 로그를 남기지 않는다.

### 4.3 CLI

| CLI | 범위 | 기본값 |
| --- | --- | ---: |
| `--rtsp-frame-id-sei=0|1` | 운영 및 시험 | `0` |
| `--v4l2-sync-trace-sec=N` | `0..3600`초 | `0` |
| `--channel-sync-trace-sec=N` | `0..3600`초 | `0` |
| `--rtsp-sync-trace-sec=N` | `0..3600`초 | `0` |
| `--rtsp-test-stall-ch=N` | `0..3` | `-1` |
| `--rtsp-test-stall-after-sec=N` | `0..3600`초 | `0` |
| `--rtsp-test-stall-duration-sec=N` | `1..3600`초 | `0` |

stall은 세 설정값뿐 아니라 `--test=1`도 함께 지정해야 활성화된다.

## 5. 런타임 동작

### 5.1 Frame ID SEI

`rtsp_frame_id_sei_enabled()`의 환경변수 cache를 제거하고 확정된
`cmdArg.rtsp_frame_id_sei`를 사용한다.

```text
rtsp_frame_id_sei == FALSE
  → 기존 encoded AU를 그대로 appsrc에 push

rtsp_frame_id_sei == TRUE && codec == H.265
  → GSTSYNC1 SEI 생성 및 삽입
  → 삽입 결과를 appsrc에 push
```

H.264에서 활성화를 요청하면 `check_arg()`가 시작 시 한 번 경고하고 설정을
`FALSE`로 정규화한다. 따라서 frame마다 삽입 실패를 반복하지 않는다.

### 5.2 V4L2 및 channel 계측

`videoBin.cpp`와 `encoderBin.cpp`는 환경변수 대신 각각 확정된 trace 시간값을
확인한다. 값이 `0`이면 pad를 조회하거나 trace 객체를 할당하지 않고 즉시
반환한다.

계측이 활성화된 경우의 probe 위치와 수집 항목은 현재 구현을 유지한다.

### 5.3 RTSP 계측과 overrun callback

`rtsp_sync_trace_sec == 0`이면 다음 작업을 모두 생략한다.

- `RtspSyncTrace` 할당
- appsrc/payloader/RTP 계측 probe 설치
- `rtsp_out_queue` 검색
- `overrun` signal callback 연결
- 프레임별 RTSP 계측 기록

`rtsp_sync_trace_sec > 0`일 때만 `rtsp_out_queue`의 `overrun` signal을
연결한다. callback은 기존과 같이 trace counter만 갱신하며 queue의 drop 정책을
변경하지 않는다.

### 5.4 stall 시험

stall은 RTSP trace와 독립적으로 활성화할 수 있다. 다음 조건을 모두 만족할 때
대상 channel의 payloader src pad에 stall probe만 설치한다.

1. `levelMode == MODE_TEST`
2. channel이 `0..3` 범위이고 활성 channel임
3. `after_sec`가 `0..3600` 범위임
4. `duration_sec`가 `1..3600` 범위임

일반 mode에서는 stall CLI가 입력돼도 경고 후 전체 stall 설정을 비활성화한다.
부분 설정이나 범위를 벗어난 값도 같은 방식으로 비활성화한다.

## 6. 코드 경계

### `parser.h`

- 기본값과 유효 범위 상수 정의

### `util.h`

- `CmdArg` 필드 추가

### `parser.cpp`

- `init_arg()` 기본값 초기화
- `rtsp_tune.frame_id_sei` 선택 JSON parsing
- 신규 CLI option 등록
- `check_arg()` 범위와 codec/test-mode 검증
- 최종 유효 설정 시작 로그

### `videoBin.cpp`

- V4L2 trace 환경변수 parsing 제거
- `cmdArg.v4l2_sync_trace_sec` 사용

### `encoderBin.cpp`

- channel trace 환경변수 cache 제거
- `cmdArg.channel_sync_trace_sec` 사용

### `rtspServerBin.cpp`

- SEI, RTSP trace와 stall 환경변수 parsing 제거
- `cmdArg` 기반 활성 판정
- trace와 stall probe 설치 조건 분리
- RTSP trace가 있을 때만 overrun callback 연결

### 시험 스크립트 및 문서

- 환경변수 전달을 해당 CLI option으로 변경
- 실행 로그에 사용한 JSON/CLI 설정을 기록
- `docs/CAMERA_SYNC_VALIDATION.md`와
  `docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md`의 활성화 방법 갱신

## 7. 오류 처리

- 잘못된 optional JSON은 시작을 중단하지 않고 경고 후 기본값을 사용한다.
- trace 시간 범위 오류는 해당 trace를 `0`으로 비활성화한다.
- SEI 값이 `0`/`1` 이외이면 `FALSE`로 정규화한다.
- H.264에서 SEI 요청은 경고 후 비활성화한다.
- 일반 mode 또는 잘못된 stall 조합은 경고 후 stall 전체를 비활성화한다.
- probe/callback 설치 실패는 기존 `__LOG` 형식으로 기록하고 영상 pipeline은
  가능한 경우 계속 동작한다.

## 8. 호환성과 영향

- 신규 JSON key와 CLI를 사용하지 않으면 영상 pipeline과 bitstream은 기존과
  동일하게 동작한다.
- 모든 trace가 `0`이면 계측 pad probe와 overrun callback이 설치되지 않는다.
- SEI 기본값은 `FALSE`이므로 기존 RTSP client가 동일하게 동작한다.
- `gstreamer-codecparsers-1.0`은 SEI 운영 기능을 runtime에서 선택할 수 있도록
  계속 build/runtime 의존성으로 유지한다.
- 기존 환경변수를 사용하는 시험 스크립트는 신규 CLI 형식으로 갱신해야 한다.

## 9. 검증 계획과 합격 기준

### 9.1 parser

1. 새 key가 없는 기존 JSON에서 모든 신규 설정이 기본값을 유지한다.
2. JSON `frame_id_sei=true`가 적용된다.
3. CLI `--rtsp-frame-id-sei=0`이 JSON `true`를 덮어쓴다.
4. 잘못된 JSON 타입과 CLI 범위가 경고 후 안전한 값으로 정규화된다.
5. H.264 SEI 요청과 일반 mode stall 요청이 비활성화된다.

### 9.2 기본동작 OFF

1. 모든 신규 옵션을 생략하고 4채널 FHD 15fps pipeline을 실행한다.
2. V4L2/channel/RTSP probe와 overrun callback이 설치되지 않았음을 로그로
   확인한다.
3. RTSP 네 channel 수신, PTS/RTP timestamp 단조 증가와 기존 queue 복구를
   확인한다.
4. Frame ID client가 SEI 부재를 검출하는지 확인한다.

### 9.3 기능별 ON

1. H.265 SEI ON에서 4채널 Frame ID group mismatch, gap, duplicate와 삽입
   실패가 0인지 확인한다.
2. 세 trace CLI가 각각 지정 시간 후 요약 로그를 남기는지 확인한다.
3. RTSP trace ON에서만 overrun callback이 연결되고 counter가 집계되는지
   확인한다.
4. test mode stall이 지정 channel에만 적용되고 client가 재동기화되는지
   확인한다.

### 9.4 회귀 및 배포

1. `./make-for-imx8` 크로스 빌드가 성공해야 한다.
2. 수정 OFF binary와 기존 운영 binary의 정상망 smoke 결과를 비교한다.
3. 시험 종료 후 target의 원본 binary, `cam-operate.service`, video FD와 시험
   process가 정상 복원됐는지 확인한다.

## 10. 완료 조건

- 환경변수 기반 활성 경로가 코드와 시험 스크립트에서 제거됨
- JSON·CLI 우선순위와 최종 설정이 시작 로그에서 확인됨
- trace OFF에서 overrun callback을 포함한 모든 계측 hook이 미설치됨
- 기본동작 OFF 및 기능별 ON 검증 통과
- 관련 한글 시험 문서 갱신
