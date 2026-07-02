# Ship 리뷰 결정 기록 — PR #26 (feat/capture-instant-snapshot)

자동 리뷰어 지적에 대한 검증 결과와 처리. 재리뷰 라운드에서 동일 지적이 재등장해도
아래 근거로 판정된 건은 다시 블로킹하지 않는다.

## 적용한 수정 (round 1 → round 2)

- **[HIGH · Gemini Assist] `be.valve` teardown UAF** — FIXED.
  프로브한 valve sink pad를 `probe_pad` 멤버로 **소유 ref** 보관. 소멸자는 `be.valve` 대신
  `probe_pad`로 probe 제거 후 unref → pipeline이 `be.bin`/`be.valve`를 먼저 해제해도 안전.
- **[P2 · Codex] clamp가 `cmdArg`에 미전파** — FIXED.
  `check_arg()`는 `cmdArg = parser->arg`(main.cpp:749) 이후 실행되어 clamp가 `cmdArg`에
  반영 안 됨. 소비 지점(captureBin)에서 `cap_instant_enabled()`(== 1 또는 2)로 명시 게이트.
- **(선제) push-buffer 소유권 모호성 제거** — `g_signal_emit_by_name("push-buffer")` +
  `unref` → 문서상 transfer-full인 `gst_app_src_push_buffer()` C API로 전환(unref 제거).

## 반려한 False Positive (근거 기록 — 재litigate 금지)

- **[CRITICAL · Gemini Auto] 소멸자 `be` 컴파일 에러** — DECLINED.
  `be`는 지역변수 아님. `CaptureElement be;`는 **public 멤버**(captureBin.h). 생성자
  (`be.bin=NULL`), `getState()`, `setQueueSize()`, `addBinToPipe()` 등이 이미 사용. 컴파일 정상.
- **[CRITICAL · Claude] `inject_last_src_buffer` push-buffer 이중해제** — DECLINED (원인 코드는
  선제적으로 C API 전환으로 정리). 판정 근거: **액션 시그널**은 `gst_app_src_push_buffer_action`
  → `push_buffer_full(steal_ref=FALSE)` = **transfer-none**(C 함수 `gst_app_src_push_buffer`의
  transfer-full과 다름). 결정적 증거: 기존 코드(captureBin.cpp:548→556)가 sample 소유 버퍼를
  push 후 `gst_sample_unref(sample)`을 하는데, transfer-full이면 매 프레임 이중해제로 이미
  크래시했을 것 — 정상 동작이 transfer-none을 증명. (라운드2에서 C API로 바꿔 논점 자체 제거.)
- **[Critical · OpenCode] 이중 캡처(2 파일)** — DECLINED.
  `maxCnt=1` + `on_new_sample_to_file`의 `queue_mutex` 직렬 처리 → 먼저 온 프레임이 요청 완료·
  `current_request=null`·valve close, 뒤 프레임은 no-request 드롭 = 파일 1개. (Claude도 동일 확인.)
- **[Critical · OpenCode] worker 스레드의 probe UAF** — DECLINED.
  worker(`capture_worker_thread`)는 `last_src_buffer`/`last_src_mutex`를 **전혀 접근 안 함**
  (task->sample/file_path/request만). 소멸자 순서(probe 제거 → 정리) 정상. (Claude도 확인.)
- **[MEDIUM · Gemini Auto] `req` NULL 가능성** — DECLINED.
  `req = info->current_request`가 non-NULL로 확정된 else-분기 내부에서만 참조 → 항상 유효.

## Advisory (유효하나 비블로킹 — 이미 문서화된 트레이드오프)

- [HIGH · Gemini Auto] mode-1(ref) 풀 점유 위험 / [MEDIUM] mode-2(copy) 유휴 CPU — 기능은
  기본 OFF opt-in이고 모드 트레이드오프는 코드 주석·PR·`--capinstant` 도움말에 명시. 온-타겟
  soak(record+RTSP+capture 동시)로 mode-1 풀 안전성 검증 예정.
- [LOW] appsrc `block=FALSE` 명시 호출 — 주석으로 의도 고정(데드락 방지 근거 명시).
