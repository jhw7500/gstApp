# PR #38 범위 한정 리뷰 후속 검증 기록

## 1. 목적과 범위

이 문서는 PR #38의 광범위 자동 리뷰 결과를 현재 코드와 API 계약에 다시 대조한 뒤,
실제로 수용한 세 가지 보완과 그 검증 근거를 2026-08-14 기준으로 고정한다. 광범위
리뷰의 `Critical`/`High` 지적에는 현재 소유권·수명·API 계약과 맞지 않는 오탐이 많았다.
검증 가능한 문제까지 한 번에 고치면 오탐 대응과 실제 회귀가 섞이므로, 다음 세 항목을
서로 독립된 설계·테스트·커밋으로 나누고 각 커밋 범위만 다시 검토했다.

1. 인코더 계측값의 스트리밍 스레드 쓰기와 메인 루프 읽기 사이 데이터 경쟁 제거
2. V4L2 프레임별 로그를 명시적 활성화로 분리
3. `make-for-imx8`의 인자 재분할과 SDK 초기화 실패 전파 문제 제거

거절한 리뷰 주장은 모두를 반복하지 않는다. 요지는 현재 코드의 객체 수명과 실제 API
계약을 소스에서 확인했을 때 재현 근거가 없었던 `Critical`/`High` 주장은 수정 사유로 채택하지
않았다는 것이다. 반대로 아래 세 항목은 소스 계약, 실패 재현 또는 측정 가능한 비용으로
입증되어 수용했다.

## 2. 검증 기준

| 항목 | 값 |
| --- | --- |
| 저장소/브랜치 | `/home/jhw/ai/opencode/projects/gstApp`, `fix/split-skew-and-channel-diagnostics` |
| 검증 HEAD | `854f9b4372bcc9f41db6f3e185120626c1be2dd7` |
| 대상 장치 | `root@192.168.214.4` (`pim-camera-v016`, i.MX8, aarch64) |
| SDK | `/shared/fsl-imx-xwayland/5.10-hardknott` |
| 도구 모음 | `cortexa53-crypto-poky-linux` |
| 검증 `bin/gstApp` SHA-256 | `4539a8d30af83a06efd0fc4ebd657e780bff1dc5e583bf2ef03010839274d344` |
| 대상 실행 시간 | `2026-08-14T02:34:19Z` ~ `02:37:15Z` |

최초 검증 기록 시점의 전체 PR #38 후속 이력은 `dcc73bf..cb76659`이다. 이 이력에는
설계·계획·검증 문서 커밋도 포함된다. 아래 일곱 SHA는 전체 범위라는 뜻이 아니라,
실제 구현과 구현 계약 보강에 해당하는 정확한 커밋만 추린 것이다.

| 커밋 | 내용 |
| --- | --- |
| `8ebb3e5ec17bb12bbd0234ad16ffe53a529728ad` | 원자적 인코더 계측과 순수 단위 시험 추가 |
| `c611f6f0803795fc8ae724d55517ed97bde6a8da` | 계측을 `EncoderBin`에 연결 |
| `9ecb59949c0cf4d63df38b810eca17b0de6a8fff` | V4L2 프레임 로그 명시 활성화 구현 |
| `b6b418a78488d26910e947404d120d590d386863` | GOption/상태 복사 계약과 문서 개수 검증 보강 |
| `cb9598b3c057bd5a765e3cd8e45be3e925764ca3` | `|=`, `++` 등 프레임 로그 변이 누락 방지 |
| `541c662a452b71c5c76346411b1e805928190d60` | 래퍼 인자와 pkg-config 환경 보존 |
| `854f9b4372bcc9f41db6f3e185120626c1be2dd7` | SDK 환경 스크립트 읽기 실패 상태 전파 |

## 3. 수용한 문제와 동작 영향

### 3.1 Encoder 계측 동기화

- `q_in`, `q_out`, `enc_out`, `overrun`은 완화 순서 원자 증가/읽기로 바꾸고,
  `lvl_buf_max`, `enc_gap_max_us`는 완화 순서 compare/exchange 최댓값으로 바꿨다.
- 보고 함수는 채널별 지역 스냅샷 한 번을 증분, 이전 값 갱신, 최댓값 출력에 재사용한다.
  `lvl_buf_max`는 누적 최고 수위로 유지하고 `enc_gap_max_us`만 보고 구간마다 0으로
  교환한다.
- `queue`, `active`, 단일 작성자의 `enc_last_us`는 기존 수명 규칙대로 유지했다.
- 기본 실행 경로에는 mutex가 추가되지 않았다. 항상 수행되던 queue 입력 증가 한 번이
  완화 순서 원자 증가 한 번으로 바뀌며, 상세 통계의 활성화 조건과 출력 의미는 유지된다.

### 3.2 V4L2 프레임 로그 명시 활성화

- CLI 전용 `--v4l2-sync-log-frames=0|1`을 추가했고 기본값은 `0`이다. JSON
  스키마에는 추가하지 않았다.
- 기본 `trace=0, log=0`에서는 probe와 프레임 로그가 모두 없다.
- `--v4l2-sync-trace-sec=N`만 사용하면 연속성 계측과 summary는 실행하지만 프레임별
  로그는 남기지 않는다. 정확한 프레임별 로그가 필요한 시험만
  `--v4l2-sync-log-frames=1`을 함께 지정한다.
- 잘못된 불리언은 경고 후 `0`으로, 유효 trace가 없는 프레임 로그 요청도 경고 후
  `0`으로 정규화한다. 이 과정이 trace를 암묵적으로 켜지는 않는다.
- 따라서 제품 기본 파이프라인은 그대로이고, 고비용 프레임별 syslog I/O만 명시적
  활성화 대상이 되었다.

### 3.3 i.MX8 빌드 래퍼

- SDK 경로와 변수를 인용하고 `PKG_CONFIG_SYSROOT_DIR`, `PKG_CONFIG_PATH`, 빈
  `PKG_CONFIG_DIR`을 export했다.
- `make "$@"`로 공백과 literal glob을 포함한 인자 및 인자 0개 호출을 그대로 전달한다.
- SDK 환경 스크립트가 실패하면 그 상태값을 그대로 반환하고 `make`를 실행하지
  않는다.
- 제품 실행 동작에는 영향이 없고 빌드 진입점의 argv/env 및 실패 계약만 강화했다.

## 4. 로컬·소스·ShellCheck·빌드 게이트

이 추적 검증 기록과 커밋된 산출물에는 비밀번호가 없다. 대상 인증값은 실행 환경 외부에서
주입했으며, 자격증명을 포함할 수 있는 로컬 원시 증거는 배포하거나 커밋하지 않는다.

| 구분 | 실행 명령/검사 | 결과 |
| --- | --- | --- |
| 차이 | `git diff --check` 및 각 커밋의 반영된 차이 검사 | 종료 0, 진단 0 |
| wrapper 계약 | `test/run-make-for-imx8-test.sh` | argv 2개/0개, 공백 경로, literal glob, pkg-config 환경, SDK `return 37` 전파 모두 통과 |
| shell 문법 | `bash -n make-for-imx8 test/run-cfgjson-test.sh test/run-make-for-imx8-test.sh test/run-sync-config-cli-test.sh test/run-sync-config-source-check.sh update_bin.sh` | 6개 모두 종료 0 |
| ShellCheck | `shellcheck --severity=warning --format=gcc make-for-imx8 test/run-cfgjson-test.sh test/run-make-for-imx8-test.sh test/run-sync-config-cli-test.sh test/run-sync-config-source-check.sh update_bin.sh` | ShellCheck 0.8.0, 진단 0 |
| CI 조건부 검사 | 존재하는 `dist/wlan/DEBIAN/{postinst,postrm,prerm}`에 `shellcheck -x -s bash` | 대상 파일 0개로 CI 존재 조건과 일치 |
| 소스 계약 | `test/run-sync-config-source-check.sh` | `sync config source contract: PASSED` |
| 깨끗한 빌드 | `./make-for-imx8 clean && ./make-for-imx8 -j4` | 종료 0, aarch64 `bin/gstApp` 생성 |
| 순수 시험 빌드 | `./make-for-imx8 bin/testRtspSync bin/testRtspValidation bin/testCfgjson bin/testEncoderStat` | 네 aarch64 실행 파일 생성 |
| 대상 help 계약 | `BOARD=192.168.214.4 BOARD_PW="${BOARD_PW:?외부 주입 필요}" bash test/run-sync-config-cli-test.sh` | 100644 파일도 깨끗한 체크아웃에서 실행 가능, 8개 필수 도움말 옵션 확인, 임시 파일 삭제 |
| cfgjson 독립 게이트 | `BOARD=192.168.214.4 BOARD_PW="${BOARD_PW:?외부 주입 필요}" bash test/run-cfgjson-test.sh` | 외부 인증값으로 실행, `30 checks, 0 failures` |

순수 대상 시험은 각 파일을 고유한 `/tmp/omx-1786606350624-aggregate-*` 경로로
복사하기 전후 SHA-256 일치를 확인하고 순차 실행했다.

| 실행 파일 | SHA-256 | 결과 |
| --- | --- | --- |
| `testRtspSync` | `e8d1282cf9c3c926758f4d8b21eea2638330a85eeb78027086bf2685d3a11eb6` | 63개 검사, 실패 0 |
| `testRtspValidation` | `cc3a1b653622c1661ea6d560c238f916c37c518081889dd72b3003adef61b240` | 92개 검사, 실패 0 |
| `testCfgjson` | `07ed72da415c4dbecc3008e75f45aac2796a9926c4bdcc9f6f00bebd21ebe52e` | 30개 검사, 실패 0 |
| `testEncoderStat` | `1e1ea7981c25ee814e2f3c137f9d81abccd52de70d6ab2d46b2aafe6595905bd` | 30개 검사, 실패 0 |

합계는 `63 + 92 + 30 + 30 = 215`개 검사, 실패 `0`이다. 네 aggregate 파일과
help/cfgjson 임시 파일의 최종 개수도 `0`이었다.

## 5. 대상 V4L2 5단계 실행 검증

모든 단계는 `/root`에서 고유 배포 파일
`/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4`을 한 번에 하나만
`-d 22 -m 4`로 실행했다.

```text
# 0. 기본값
/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4 -d 22 -m 4

# 1. trace 활성, 프레임 로그 비활성
/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4 -d 22 -m 4 --v4l2-sync-trace-sec=3 --v4l2-sync-log-frames=0

# 2. trace 활성, 프레임 로그 활성
/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4 -d 22 -m 4 --v4l2-sync-trace-sec=3 --v4l2-sync-log-frames=1

# 3. 잘못된 불리언
/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4 -d 22 -m 4 --v4l2-sync-trace-sec=3 --v4l2-sync-log-frames=2

# 4. 잘못된 trace와 의존성
/tmp/gstApp-v4l2-final-omx1786606350624-854f9b4 -d 22 -m 4 --v4l2-sync-trace-sec=-1 --v4l2-sync-log-frames=1
```

각 단계의 첫 health poll에서 프로세스 생존, video FD `4`개, 시험 PID 소유 TCP
`8554` listener를 모두 확인했다. journal은 `_PID=<시험 PID>`와 단계 시작 시각으로
분리했다.

| 단계 | CSI | enabled | sample | summary |
| --- | ---: | ---: | ---: | ---: |
| 기본값 | 0 | 0 | 0 | 0 |
| 기본값 | 1 | 0 | 0 | 0 |
| trace 활성/로그 비활성 | 0 | 1 | 0 | 1 |
| trace 활성/로그 비활성 | 1 | 1 | 0 | 1 |
| trace 활성/로그 활성 | 0 | 1 | 45 | 1 |
| trace 활성/로그 활성 | 1 | 1 | 45 | 1 |
| 잘못된 불리언 | 0 | 1 | 0 | 1 |
| 잘못된 불리언 | 1 | 1 | 0 | 1 |
| 잘못된 trace/의존성 | 0 | 0 | 0 | 0 |
| 잘못된 trace/의존성 | 1 | 0 | 0 | 0 |

summary가 생성된 세 단계의 두 CSI 모두 `lost=0`, `sequence_resets=0`,
`pts_backwards=0`이었다. 로그 활성 단계는 CSI별 sample `45`개, 전체 `90`개였고,
로그 비활성과 잘못된 불리언 단계는 summary 상태 갱신은 유지하면서 sample이 `0`이었다.

## 6. 실행 수명주기 차단, 안전 게이트와 복구 증거

### 6.1 최초 차단과 소유자 확인

첫 시도 전 읽기 전용 기준에는 서비스가 `inactive/disabled`인 상태에서 수동 실행된
운영 PID `43609`, video FD 4개, 8554 listener가 있었다. 실행 직전 PID가
`112919`로, 다음 확인에서는 `113928`로 바뀌었으므로 하네스가 운영 프로세스 신호
지점 전에 fail-closed했다. 배포 파일만 지웠고 서비스 조작, 운영 프로세스 신호,
binary/config 쓰기는 하지 않았다.

후속 읽기 전용 관찰에서는 `gstApp`이 12회 연속 `0`, 8554 listener가 `0`이었다.
PID `639`의 `/bin/login -p --`와 PID `934`의 root `-bash`가 `ttymxc1`에 있었고,
교체되던 수동 프로세스도 PPID `934`의 사용자 세션에 속했다. 따라서 이전 실행은
systemd나 supervisor가 아니라 사람의 serial console 수명주기에서 시작된 것으로
확인했다. 이후에만 명시적인 프로세스 0개 통제 창을 기준으로 새 하네스를 실행했다.

### 6.2 실행 중 안전과 종료

- `EXIT`, `HUP`, `INT`, `TERM` trap을 설치하고 각 단계 전후 서비스, production/config
  SHA, PID 934, 외부 `gstApp`, 8554, 배포 파일 SHA를 재검사했다.
- 시험 식별정보는 PID, 정확한 `/proc/$pid/exe`, 정확한 `argv[0]`, starttime 네 항목이
  모두 맞을 때만 인정했다. 모든 `/proc`을 poll해 외부 `gstApp` 출현 시 차단하도록 했다.
- 다섯 시험 PID 모두 정확한 식별정보를 확인한 뒤 `TERM`만으로 약 1초 안에 종료했다.
  `KILL`은 사용하지 않았고 외부 프로세스에는 신호를 보내지 않았다.
- 다섯 단계 사이 guard와 최종 guard가 모두 통과했다. 고유 binary, 원격 하네스,
  증거 디렉터리, aggregate/help 임시 파일은 모두 삭제 후 부재를 확인했다.
- `cam-operate.service`를 start/stop/restart/enable/disable하지 않았고,
  `/usr/local/bin/gstApp`, `/root/shared_v/edgeconf_pim.json`을 쓰거나 교체하지 않았다.
  `192.168.214.8`에는 접속하지 않았다.

### 6.3 최종 복구 상태

하네스 종료 후 2초 간격으로 세 번 독립 재검사했으며 모두 다음과 같았다.

| 항목 | 최종 값 |
| --- | --- |
| `cam-operate.service` | `inactive/dead/disabled` |
| `gstApp` 유사 프로세스 | `0` |
| TCP 8554 listener | `0` |
| 운영 binary SHA-256 | `babde7d6626360894eb0254f8230df1e4c8574bd50e6f05f2aa7d128f52fb109` |
| config SHA-256 | `37f7f621d92e2d6d2f7c1e7666423194712eb346323105d6e86239b1b0217108` |
| 실행 잔여물 | `0` |

이 최종 프로세스 0개 상태는 통제 시험 직전 관찰한 기준 상태와 일치한다. 이는 정확한
원복 증거이지, 운영 기능이 서비스를 제공 중이라는 주장이 아니다. 실제 최종 상태는
서비스 inactive, `gstApp=0`, 8554 listener `0`이므로 **운영 서비스 비제공 상태**다.

## 7. 제한 사항과 잔여 경고

- 깨끗한 교차 빌드에는 수정하지 않은 기존 코드의 경고 8개가 남았다:
  `muxSinkBin.cpp`의 미사용 변수 2개와 `write()` 반환값 무시 1개,
  `rtspServerBin.cpp`의 미사용 변수 2개, `tcpServer.cpp`의 미사용 변수 1개,
  `ipc.cpp`의 미사용 함수 1개, `main.cpp`의 값 설정 후 미사용 변수 1개다. 수정한
  encoder/parser/video/wrapper 범위에서 새 컴파일러 경고는 없었다.
- 대상 검증은 3초 trace와 제한된 시작/식별정보 poll로 수행한 제한 시간 연기 시험이다.
  장시간 운용, 최대 부하, 재부팅 후 자동 기동, 운영 서비스 가용성을
  입증하지 않는다.
- 당시 채널 미기동 진단 handoff는 현재 동작과 구분되는 역사 기록으로 정리해
  `docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md`에서 추적한다.
