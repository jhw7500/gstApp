# PR #38 리뷰 후속 보완 설계

## 목적

PR #38 자동 리뷰를 현재 코드와 API 계약에 대조한 결과, Critical/High 지적은
오탐이었지만 다음 세 항목은 독립적으로 보완할 가치가 있다.

1. `g_encStat`의 streaming thread 쓰기와 main-loop timer 읽기 사이 data race 제거
2. V4L2 frame별 trace log를 명시적인 opt-in으로 분리
3. 기존 `make-for-imx8`의 인자 재분할 문제와 ShellCheck 실패 제거

각 항목은 테스트와 커밋을 분리한다. 한 항목의 실패를 다른 수정과 섞지 않으며,
최종 PR 리뷰도 새 커밋 범위별로 먼저 수행한다.

## 1. Encoder 진단 상태 동기화

### 검토한 방법

- 모든 값을 하나의 mutex로 보호: 단순하지만 기본 동작에서도 frame마다 lock이
  필요하다.
- GLib mutex로 모든 값을 보호: 구현은 단순하지만 항상 설치되는 queue 입력
  probe에서 frame마다 lock/unlock이 필요하다.
- **선택안:** GStreamer 수명 객체와 설정은 startup 불변으로 유지하고, 계측
  숫자만 C++ relaxed atomic으로 분리한다.

### 선택 설계

- GStreamer 의존성이 없는 `EncoderTelemetry` 구성요소를 추가한다.
- `q_in`, `q_out`, `enc_out`, `overrun`은 `std::atomic<guint64>`의 relaxed
  increment/load를 사용한다. 기본 경로 비용은 기존 증가 1회를 relaxed atomic
  증가 1회로 바꾸는 데 한정된다.
- `lvl_buf_max`와 `enc_gap_max_us`는 relaxed compare/exchange max를 사용한다.
  `lvl_buf_max`는 누적 watermark로 보존하고 `enc_gap_max_us`만 report snapshot에서
  `exchange(0)`으로 다음 구간을 시작한다.
- `enc_last_us`는 동일 encoder source pad의 단일 writer가 이전 시각 계산에만
  사용하므로 callback-local channel 상태로 유지한다.
- timer는 각 값을 한 번만 snapshot하고 그 snapshot으로 delta와 `prev`를 모두
  계산한다. 여러 counter의 동일 순간 snapshot은 요구하지 않으며 기존처럼
  일시적인 음수 leak은 0으로 제한한다.
- `cmdArg`, queue pointer와 channel 활성 상태는 startup 이후 불변인 기존
  수명 규칙을 유지한다.

### 검증

- 실제 `EncoderTelemetry` API를 여러 writer thread가 동시에 호출해 최종
  counter가 정확한지 확인한다.
- 상세 통계 OFF에서도 heartbeat가 증가하고 상세 counter는 증가하지 않는지
  확인한다.
- relaxed CAS max, 누적 watermark 보존과 구간 gap reset을 확인한다.
- target에서 pure unit test를 실행하고 전체 i.MX8 cross-build를 통과시킨다.

## 2. V4L2 frame log 분리

### 검토한 방법

- 기존 동작 유지 후 경고만 추가: log I/O 부하를 실제로 제어하지 못한다.
- N개마다 sampling: sequence/PTS의 정확한 frame별 상관관계를 잃는다.
- **선택안:** 요약 trace와 frame별 log를 별도 CLI로 제어한다.

### 선택 설계

- `--v4l2-sync-trace-sec=N`은 기존처럼 probe와 연속성 요약을 활성화한다.
- 새 CLI-only 설정 `--v4l2-sync-log-frames=0|1`을 추가하며 기본값은 `0`이다.
- 값이 `1`일 때만 각 frame의 sequence/PTS log를 남긴다. 따라서 정밀 측정은
  기능을 잃지 않고 명시적으로 활성화할 수 있다.
- trace가 0인데 frame log만 1이면 frame log를 비활성화하고 경고한다.
- JSON에는 추가하지 않는다. 기존 정책대로 trace/stall은 시험용 CLI이고,
  제품 JSON에는 `rtsp_tune.frame_id_sei`만 둔다.
- `check_arg()` 이후의 최종 `sync_config` log에 `v4l2_log_frames`를 포함한다.

### 호환성

- 제품 기본값은 trace 0이므로 기본 pipeline 동작은 바뀌지 않는다.
- 기존에 `--v4l2-sync-trace-sec`만 사용해 frame log를 수집한 시험 명령에는
  `--v4l2-sync-log-frames=1`을 추가해야 한다.
- 문서의 현재 명령과 옵션 표를 갱신하되 과거 원시 시험 조건은 변경하지 않는다.

### 검증

- parser/source 계약 테스트를 먼저 실패시킨 뒤 CLI, 기본값, 정규화와 최종
  설정 log를 구현한다.
- target help contract에서 새 옵션을 확인한다.
- 짧은 target trace에서 OFF는 summary만, ON은 frame log와 summary를 모두
  생성하는지 확인한다.

## 3. Cross-build wrapper와 CI

### 선택 설계

- SDK script 경로와 변수 확장을 quote한다.
- `PKG_CONFIG_SYSROOT_DIR`와 `PKG_CONFIG_PATH`를 명시적으로 export한다.
- 빈 `PKG_CONFIG_DIR`은 `PKG_CONFIG_DIR=''`로 표현한다.
- `make $@`를 `make "$@"`로 바꿔 공백과 glob 문자가 포함된 인자를 그대로
  전달한다.
- 동적 Yocto SDK source에는 범위를 한정한 ShellCheck 설명을 둔다.

### 검증

- fake SDK와 fake `make`를 사용해 공백/glob 인자가 하나의 인자로 보존되고
  pkg-config 환경값이 전달되는지 확인하는 shell test를 먼저 추가한다.
- 수정 전 이 테스트와 ShellCheck가 기대한 이유로 실패하는 것을 확인한다.
- 수정 후 새 test, 전체 shell script ShellCheck와 i.MX8 cross-build를
  통과시킨다.

## 커밋과 종료 조건

다음 순서로 서로 독립된 커밋을 만든다.

1. 설계 문서
2. encoder telemetry 동기화와 unit test
3. V4L2 frame log opt-in과 설정/문서/test
4. cross-build wrapper와 shell test

각 코드 커밋은 해당 커밋 diff만 대상으로 별도 리뷰한다. 마지막에는 전체
cross-build, pure target tests, 짧은 기본/trace target smoke test를 순차
실행한다. 타겟 시험은 기존 운영 process/binary/config checksum을 기록하고
trap 기반으로 원복하며, 원복이 확인되지 않으면 성공으로 보고하지 않는다.
