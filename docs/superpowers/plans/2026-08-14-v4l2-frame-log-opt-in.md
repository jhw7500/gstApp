# V4L2 Frame Log Opt-in Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** V4L2 연속성 summary와 frame별 syslog를 분리하여 상세 로그를 명시적으로 요청한 시험에서만 출력한다.

**Architecture:** 기존 trace 시간은 probe와 summary를 제어한다. 새 CLI-only boolean은 frame log만 제어하며 `check_arg()`에서 유효값과 trace 의존성을 정규화한다.

**Tech Stack:** C++, GLib GOption, GStreamer, shell source/CLI contracts

## Global Constraints

- JSON 스키마에는 새 값을 추가하지 않는다.
- 기본값은 `FALSE`이고 기존 제품 기본 pipeline에는 probe/log를 추가하지 않는다.
- 과거 측정 기록은 재작성하지 않는다.
- build는 반드시 `./make-for-imx8`을 사용한다.

---

### Task 1: Parser and source contracts

**Files:**
- Modify: `test/run-sync-config-source-check.sh`
- Modify: `test/run-sync-config-cli-test.sh`
- Modify: `util.h`, `parser.h`, `parser.cpp`, `videoBin.cpp`
- Modify: `docs/CAMERA_SYNC_VALIDATION.md`, `docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md`

- [ ] Add failing source/help contract checks for `--v4l2-sync-log-frames`.
- [ ] Verify both fail against the old source/binary for the expected missing option/guard.
- [ ] Add `gboolean v4l2_sync_log_frames`, default `FALSE`, `G_OPTION_ARG_INT`, 0/1 validation, trace dependency normalization, and final effective log.
- [ ] Copy the setting into `V4l2SyncTrace` and guard the per-frame `__LOG` call; include `frame_log` in the enabled notice.
- [ ] Update only current option tables/examples and document that exact frame logs require both trace duration and the new flag.
- [ ] Cross-build, run source contract, target help contract, and focused target OFF/ON/invalid smoke tests.
- [ ] Commit and review only this V4L2/config diff.

