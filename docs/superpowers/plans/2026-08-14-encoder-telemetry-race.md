# Encoder Telemetry Race Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기본 encoder 입력 감시 비용을 최소화하면서 `g_encStat`의 streaming thread/main-loop data race를 제거한다.

**Architecture:** GStreamer 객체 수명과 startup 설정은 `encoderBin.cpp`에 두고, 숫자 계측만 GStreamer 비의존 `EncoderTelemetry`로 분리한다. 단일 값 계측에는 C++ relaxed atomic을 사용하고 report는 한 번 얻은 snapshot만 계산에 사용한다.

**Tech Stack:** C++, GLib types/thread test, Makefile, i.MX8 Yocto cross compiler

## Global Constraints

- 기본 queue 입력 경로에는 mutex를 추가하지 않는다.
- `lvl_buf_max`는 누적값, `enc_gap_max_us`는 report 구간값이라는 기존 의미를 유지한다.
- 새 외부 의존성이나 전역 상태를 추가하지 않는다.
- build는 반드시 `./make-for-imx8`을 사용한다.

---

### Task 1: Atomic telemetry component

**Files:**
- Create: `encoderStat.h`
- Create: `encoderStat.cpp`
- Create: `test/test_encoderStat.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces: `EncoderTelemetry`, `EncoderTelemetrySnapshot`
- `recordQueueInput()`, `recordQueueOutput()`, `recordEncoderOutput(gint64)`, `recordOverrun()`, `recordQueueLevel(guint)`
- `snapshot(gboolean reset_gap)` returns all counters and optionally exchanges only the gap maximum with zero.

- [ ] **Step 1: Write the failing concurrency and semantics test**

The test starts from zero, verifies each record method, verifies cumulative queue watermark and interval gap reset, then starts four GLib threads that add a fixed number of queue inputs and checks the exact final total.

- [ ] **Step 2: Verify RED**

Run: `./make-for-imx8 bin/testEncoderStat`

Expected: FAIL because the target and `encoderStat.*` do not exist.

- [ ] **Step 3: Implement the minimum relaxed-atomic component and Makefile target**

Use `fetch_add(..., std::memory_order_relaxed)`, `load`, `exchange`, and a compare-exchange max loop. Do not add locks or GStreamer includes.

- [ ] **Step 4: Cross-build and run on target**

Run the cross-build, deploy by checksum to `/tmp`, execute the test, and delete the deployed artifact. Expected: all checks pass with zero failures.

### Task 2: Integrate telemetry into EncoderBin

**Files:**
- Modify: `encoderBin.cpp:27-42,588-737,1247-1294`
- Modify: `Makefile:encoderBin.o dependencies`
- Test: `test/test_encoderStat.cpp`

**Interfaces:**
- Consumes: Task 1 `EncoderTelemetry` and snapshot API.

- [ ] **Step 1: Extend the source contract to reject raw shared counter accesses**

Add checks that `EncStat` contains `EncoderTelemetry`, callbacks call the record methods, and timer calculations use a local snapshot.

- [ ] **Step 2: Verify RED**

Run: `test/run-sync-config-source-check.sh`

Expected: FAIL because `encoderBin.cpp` still increments and reads raw shared fields.

- [ ] **Step 3: Replace raw fields with the helper**

Keep `queue`, `active`, and the single-writer previous encoder timestamp local to `EncStat`. Record numerical values through telemetry; snapshot once per report and use that snapshot for `prev` updates.

- [ ] **Step 4: Verify GREEN and full cross-build**

Run source contract, `./make-for-imx8 bin/testEncoderStat`, and `./make-for-imx8`. Expected: PASS; pre-existing compiler warnings must be reported separately.

- [ ] **Step 5: Commit and scoped review**

Commit only `encoderStat.*`, its test, `encoderBin.cpp`, Makefile, and the source-contract change. Review only this commit diff before starting V4L2 work.

