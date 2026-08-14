# gstApp MAX9296 Parallel Prepare Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make gstApp prepare both active MAX9296 CSI domains concurrently on
cold startup, reuse an exact retained warm configuration safely, and fail before
GStreamer state transition on partial or ambiguous hardware ownership.

**Architecture:** A standalone, dependency-light `max9296Prepare` module owns
the v1 sysfs ABI, status parser, state policy, concurrent writes, rollback, and
a lifetime gstApp owner flock. `main.cpp` acquires the flock before the parser's
MAX9296 side effects, builds the final request after VideoBin initialization may
adjust channel enables, and invokes the coordinator immediately before the
first PAUSED/PLAYING transition.

**Tech Stack:** C++11, POSIX `open/read/write/flock/clock_gettime`, pthreads,
existing GNU Make/Yocto SDK wrapper, standalone assert-style host tests, Python
source-contract tests.

## Global Constraints

- Work only on branch `feature/gstapp-max9296-prepare` in
  `/home/jhw/ai/opencode/projects/.worktrees/gstapp-max9296-prepare`.
- Keep `max9296Prepare.h/.cpp` independent of GStreamer, Glib, JSON-C, and
  `util.h`; it must compile with `-fno-exceptions -fno-rtti`.
- CSI0 is `/sys/bus/i2c/devices/2-0048/prepare` for ch0/ch1; CSI1 is
  `/sys/bus/i2c/devices/1-0048/prepare` for ch2/ch3.
- Dual-wide is one indivisible CSI request: 2560x720 or 3840x1080 with
  `enable=3`. Single is 1280x720 or 1920x1080 with `enable=1|2`.
- Use `cmdArg.main_fps[csi]`, exactly matching VideoBin caps. If both CSI
  domains are active, their FPS must match and be in `1..120`.
- The status parser requires each v1 field exactly once, accepts reordered and
  unknown future keys, and does not reject IDLE's `mode=none`/zero tuple.
- Split `ParserClass::arg_parser()` into pure parsing and one explicit
  `apply_camera_sysfs()` call. Tuple/FPS validation and the lifetime gstApp
  flock must precede every MAX9296 enable/rotate/prepare write.
- Exact CONSUMED warm reuse is permitted only while the lifetime gstApp flock
  is held. `EBUSY` is never success.
- A complete new-lease write owns rollback immediately; a refreshed
  pre-existing READY lease never does. Preserve the first failure errno even if
  rollback also fails.
- Do not asynchronously cancel a blocking sysfs write. Join every admitted
  worker; elapsed time is diagnostic, not a hard timeout.
- Do not alter parser CSI-FPS indexing, `/root/shared_v` policy, recovery
  actions, process supervision, or PR #38 RTSP/channel diagnostics.

---

## File Structure

- Create `max9296Prepare.h`: fixed-size ABI-facing types and public functions.
- Create `max9296Prepare.cpp`: POSIX backend, status parser, flock, policy,
  pthread workers, validation, and rollback.
- Create `test/test_max9296Prepare.cpp`: standalone fake-I/O behavioral tests.
- Create `test/run-max9296-prepare-test.sh`: host-only compile/run in a temporary
  directory.
- Create `test/run-max9296-prepare-source-check.sh`: main/Makefile ordering and
  wiring contract.
- Modify `Makefile`: production object and ARM64 standalone test target.
- Modify `parser.h/.cpp`: keep `arg_parser()` pure and move four MAX9296 sysfs
  writes to `apply_camera_sysfs()`.
- Modify `main.cpp`: owner-lock lifecycle, request construction, coordinator
  call/reporting, and non-zero failure exit.

### Task 1: Public types, tuple builder, and strict status parser

**Files:**
- Create: `max9296Prepare.h`
- Create: `max9296Prepare.cpp`
- Create: `test/test_max9296Prepare.cpp`
- Create: `test/run-max9296-prepare-test.sh`

**Interfaces:**
- Produces:

```cpp
enum Max9296PrepareState {
  MAX9296_STATE_IDLE,
  MAX9296_STATE_PREPARING,
  MAX9296_STATE_READY,
  MAX9296_STATE_STALE,
  MAX9296_STATE_CONSUMED,
  MAX9296_STATE_FAILED,
  MAX9296_STATE_EXPIRED
};

enum Max9296PrepareAction {
  MAX9296_ACTION_SKIPPED,
  MAX9296_ACTION_LEGACY,
  MAX9296_ACTION_WARM_REUSED,
  MAX9296_ACTION_READY_REFRESHED,
  MAX9296_ACTION_COLD_PREPARED,
  MAX9296_ACTION_FAILED
};

struct Max9296PrepareInput {
  uint32_t width;
  uint32_t height;
  uint32_t fps[2];
  uint8_t channel_enabled[4];
  uint64_t generation;
};

struct Max9296PrepareStatus {
  Max9296PrepareState state;
  uint64_t generation;
  uint64_t epoch;
  char mode[16];
  char table[16];
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  uint32_t code;
  uint32_t enable;
  int last_errno;
  int worker_errno;
  uint32_t lease;
  uint32_t match;
};

struct Max9296PrepareTarget {
  bool active;
  unsigned csi;
  const char *path;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  uint32_t enable;
  const char *mode;
  const char *table;
};

int max9296_prepare_build_targets(const Max9296PrepareInput *input,
                                  Max9296PrepareTarget targets[2]);
int max9296_prepare_parse_status(const char *line, size_t length,
                                 Max9296PrepareStatus *status);
const char *max9296_prepare_path(unsigned csi);
```

- [ ] **Step 1: Write RED tuple and parser tests**

Add tests that assert:

```cpp
Max9296PrepareInput in = {};
in.width = 1920;
in.height = 1080;
in.fps[0] = in.fps[1] = 15;
in.channel_enabled[0] = in.channel_enabled[1] = 1;
in.channel_enabled[2] = 0;
in.channel_enabled[3] = 1;
in.generation = 77;

Max9296PrepareTarget target[2] = {};
CHECK(max9296_prepare_build_targets(&in, target) == 0);
CHECK(target[0].width == 3840 && target[0].enable == 3);
CHECK(target[1].width == 1920 && target[1].enable == 2);
CHECK(strcmp(target[0].path,
             "/sys/bus/i2c/devices/2-0048/prepare") == 0);
CHECK(strcmp(target[1].path,
             "/sys/bus/i2c/devices/1-0048/prepare") == 0);
```

Cover HD/FHD dual, left/right single, disabled CSI, generation zero, invalid
dimension/mask, FPS 0/121, and active CSI FPS mismatch. Parse the real READY
sample, a CONSUMED sample with negative stored errno, reordered fields plus an
unknown key, and an IDLE `mode=none table=none` sample. Reject missing or
duplicate required keys, integer overflow, invalid signed errno, lease/match
outside 0/1, and unsupported state.

- [ ] **Step 2: Run the RED test**

Run:

```bash
bash test/run-max9296-prepare-test.sh
```

Expected: compile failure because `max9296Prepare.h/.cpp` do not exist.

- [ ] **Step 3: Implement only target construction and parsing**

Use fixed arrays and checked `strtoull`/`strtol` conversions. Required keys are:

```text
state generation epoch mode table width height fps code enable
errno worker_errno lease match
```

Accept decimal numeric fields and `0x` for `code`; require complete token
consumption and reject `ERANGE`. Do not validate request tuple against an IDLE
status inside the parser.

- [ ] **Step 4: Run the GREEN test and compiler warnings**

Run:

```bash
bash test/run-max9296-prepare-test.sh
```

Expected: all Task 1 checks pass under `-Wall -Wextra -Werror
-fno-exceptions -fno-rtti`.

- [ ] **Step 5: Commit Task 1**

```bash
git add max9296Prepare.h max9296Prepare.cpp \
  test/test_max9296Prepare.cpp test/run-max9296-prepare-test.sh
git commit -m "test: define max9296 prepare contract"
```

### Task 2: Owner flock and state-policy classification

**Files:**
- Modify: `max9296Prepare.h`
- Modify: `max9296Prepare.cpp`
- Modify: `test/test_max9296Prepare.cpp`

**Interfaces:**
- Consumes: Task 1 targets and parsed statuses.
- Produces:

```cpp
#define MAX9296_PREPARE_OWNER_LOCK "/run/lock/gstapp-camera.lock"

int max9296_prepare_acquire_owner_lock(const char *path);
void max9296_prepare_release_owner_lock(int fd);

enum Max9296PrepareDisposition {
  MAX9296_DISPOSITION_WARM,
  MAX9296_DISPOSITION_REFRESH_READY,
  MAX9296_DISPOSITION_NEW_PREPARE,
  MAX9296_DISPOSITION_FAIL
};

int max9296_prepare_classify(const Max9296PrepareTarget *target,
                             const Max9296PrepareStatus *status,
                             Max9296PrepareDisposition *disposition);
```

- [ ] **Step 1: Write RED owner and truth-table tests**

Use `mkstemp`, close/unlink it, and acquire two independent flock descriptors on
the same path. Assert first success, second `-EWOULDBLOCK`, release first, then
third success.

Add a table covering:

```text
CONSUMED lease0 exact match1 worker0 generation/epoch nonzero -> WARM
CONSUMED lease0 mismatch or match0 worker0                  -> NEW_PREPARE
CONSUMED lease0 with nonzero worker errno                   -> FAIL
READY lease1 exact match1 worker0, even errno=-ESTALE       -> REFRESH_READY
READY lease1 mismatch/match0/worker error                   -> FAIL
READY lease0                                               -> FAIL
STALE lease1                                               -> FAIL
IDLE/FAILED/EXPIRED/STALE lease0 worker0                   -> NEW_PREPARE
PREPARING                                                   -> FAIL(-EBUSY)
any non-READY lease1                                       -> FAIL(-EPROTO)
```

- [ ] **Step 2: Run RED and verify the new symbols are missing**

```bash
bash test/run-max9296-prepare-test.sh
```

- [ ] **Step 3: Implement flock and classification**

Open the lock using `O_RDWR|O_CREAT|O_CLOEXEC` and mode `0644`; use
`flock(fd, LOCK_EX|LOCK_NB)`. Return `-errno` and close on failure. Classification
must validate mode/table/width/height/fps/code `0x2006`/enable only for states
that claim a current fingerprint.

- [ ] **Step 4: Run GREEN**

```bash
bash test/run-max9296-prepare-test.sh
```

- [ ] **Step 5: Commit Task 2**

```bash
git add max9296Prepare.h max9296Prepare.cpp test/test_max9296Prepare.cpp
git commit -m "feat: classify max9296 prepare ownership"
```

### Task 3: Concurrent coordinator, retries, final validation, and rollback

**Files:**
- Modify: `max9296Prepare.h`
- Modify: `max9296Prepare.cpp`
- Modify: `test/test_max9296Prepare.cpp`

**Interfaces:**
- Consumes: Task 1/2 parser, targets, and dispositions.
- Produces:

```cpp
struct Max9296PrepareIo {
  ssize_t (*read_file)(void *context, const char *path,
                       char *buffer, size_t capacity);
  ssize_t (*write_file)(void *context, const char *path,
                        const char *buffer, size_t length);
  uint64_t (*monotonic_ns)(void *context);
  void (*sleep_ms)(void *context, unsigned milliseconds);
  int (*thread_create)(void *context, pthread_t *thread,
                       void *(*entry)(void *), void *argument);
  int (*thread_join)(void *context, pthread_t thread, void **result);
  void *context;
};

struct Max9296PrepareDomainReport {
  bool active;
  bool rollback_owned;
  Max9296PrepareAction action;
  int error;
  int rollback_error;
  uint64_t elapsed_ns;
  Max9296PrepareStatus before;
  Max9296PrepareStatus after;
};

struct Max9296PrepareReport {
  bool legacy_fallback;
  uint64_t generation;
  int error;
  Max9296PrepareDomainReport domain[2];
};

enum {
  MAX9296_PREPARE_OK = 0,
  MAX9296_PREPARE_LEGACY = 1
};

uint64_t max9296_prepare_generate_generation(void);
int max9296_prepare_all(const Max9296PrepareInput *input,
                        Max9296PrepareReport *report,
                        const Max9296PrepareIo *io);
```

- [ ] **Step 1: Write RED fake-I/O coordinator tests**

The fake backend stores one scripted status sequence per path and records every
read/write. Its two cold writes block on a condition-variable barrier until both
workers enter; a serial implementation therefore times out the test.

Cover these exact cases:

1. two cold domains use one new generation and overlap;
2. one active domain creates one worker;
3. all active reads return `-ENOENT` -> `MAX9296_PREPARE_LEGACY` with no write;
4. partial ENOENT -> failure;
5. exact CONSUMED -> warm, no prepare write, but final status re-read;
6. non-warm CONSUMED -> write, then success/`-EBUSY`/`-ESTALE` propagation;
7. exact READY with stored `errno=-ESTALE` refreshes using its original
   generation, post-status errno is zero, and it is not rollback-owned;
8. new full write success plus peer failure -> `0\n` cancel only on the new
   lease;
9. new full write success plus final read `-EIO` -> immediate cancel;
10. cancel failure records `rollback_error` but returns the original failure;
11. final generation/tuple/mode/table/worker/lease/match mismatch -> failure;
12. active domains with different or zero final epoch -> failure;
13. read/write `-EAGAIN` succeeds on attempt three, while attempt four is never
    made;
14. short positive write fails;
15. second `pthread_create` failure joins the first worker and rolls back a
    published new lease.

- [ ] **Step 2: Run RED**

```bash
bash test/run-max9296-prepare-test.sh
```

- [ ] **Step 3: Implement the coordinator**

Use these invariants:

```cpp
/* New ownership is known from the successful sysfs store itself. */
if (!preexisting_lease && write_result == command_length)
  domain.rollback_owned = true;

/* A READY refresh keeps its captured generation. */
request_generation = preexisting_lease
    ? domain.before.generation
    : report.generation;
```

Read every active status before classification and after all workers join.
Retry an individual `-EAGAIN` operation up to three total attempts with 100 ms
between attempts. Build `1 <generation> <width> <height> <fps> <enable>\n` in a
128-byte buffer and require the exact full write length. On any failure, join
all workers first, then issue `0\n` to all `rollback_owned` domains. Preserve
the first error in the return value and report.

The default backend (`io == NULL`) uses checked POSIX open/read/write/close,
`CLOCK_MONOTONIC`, and pthread create/join. Test thread hooks may fail the
second creation deterministically. No backend spawns a watchdog or cancels a
thread.

- [ ] **Step 4: Run GREEN repeatedly**

```bash
for i in $(seq 1 20); do
  bash test/run-max9296-prepare-test.sh >/dev/null || exit 1
done
echo "prepare concurrency tests stable"
```

- [ ] **Step 5: Commit Task 3**

```bash
git add max9296Prepare.h max9296Prepare.cpp test/test_max9296Prepare.cpp
git commit -m "feat: coordinate parallel max9296 prepare"
```

### Task 4: Main lifecycle wiring and build integration

**Files:**
- Modify: `main.cpp`
- Modify: `parser.h`
- Modify: `parser.cpp`
- Modify: `Makefile`
- Create: `test/run-max9296-prepare-source-check.sh`
- Modify: `test/test_max9296Prepare.cpp`

**Interfaces:**
- Consumes: `max9296_prepare_acquire_owner_lock()`,
  `max9296_prepare_generate_generation()`, and `max9296_prepare_all()`.
- Produces: production startup integration and ARM64 `bin/testMax9296Prepare`.

- [ ] **Step 1: Write RED source-order/build checks**

The Python check must assert this strict order in `main.cpp`:

```text
parser->json_parser
parser->arg_parser
last cmdArg = parser->arg
max9296_prepare_build_targets (pure preflight)
max9296_prepare_acquire_owner_lock
parser->apply_camera_sysfs
VideoBin::init call / channel-loop enable mutation
max9296_prepare_all
first GST_STATE_PAUSED or GST_STATE_PLAYING
```

Also require:

```text
prepare failure sets app_exit_code = EXIT_FAILURE before goto main_end
main_end releases owner fd
exit(app_exit_code)
Makefile OBJS contains max9296Prepare.o
Makefile has bin/testMax9296Prepare target
ParserClass::arg_parser contains no safe_write_file/default enable/rotate path
ParserClass::apply_camera_sysfs owns exactly four checked sysfs writes
```

Run:

```bash
bash test/run-max9296-prepare-source-check.sh
```

Expected: failure because production wiring is absent.

- [ ] **Step 2: Separate pure parsing from MAX9296 sysfs apply**

Move the four enable/rotate writes at the end of `ParserClass::arg_parser()`
unchanged into:

```cpp
gint ParserClass::apply_camera_sysfs();
```

The method uses the already normalized `arg.ch_enable` and `arg.ch_rotate`,
keeps existing logging/error behavior, and performs no parsing. `arg_parser()`
returns after deriving the channel fields and performs no hardware I/O.

- [ ] **Step 3: Validate, acquire owner lock, then apply sysfs**

After successful pure `arg_parser()`, `check_arg()`, and the final `cmdArg`
copy, construct `prepare_input`, generate its non-zero generation, and call
`max9296_prepare_build_targets()` as a pure preflight. If it fails, return
`EXIT_FAILURE` before any MAX9296 write. Then acquire the lock:

```cpp
gint app_exit_code = EXIT_SUCCESS;
gint max9296_owner_fd =
    max9296_prepare_acquire_owner_lock(MAX9296_PREPARE_OWNER_LOCK);
if (max9296_owner_fd < 0) {
  __LOG(LOG_CRIT, "[MAX9296_PREPARE] owner lock failed: %d",
        max9296_owner_fd);
  return EXIT_FAILURE;
}
```

Call `parser->apply_camera_sysfs()` only after the lock succeeds; on apply
failure release the fd and return `EXIT_FAILURE`. At `main_end`, release it
exactly once and finish with `exit(app_exit_code)`.

- [ ] **Step 4: Invoke prepare after post-init channel mutation**

Immediately after pipeline setup/bus-watch construction and before line 1193's
first possible state transition, rebuild the input from current `cmdArg`:

```cpp
/* prepare_input was preflighted before sysfs apply. Re-copy only channel
 * enables because VideoBin::init failure may disable a CSI domain. */
for (unsigned ch = 0; ch < 4; ++ch)
  prepare_input.channel_enabled[ch] = cmdArg.cam[ch].enable ? 1 : 0;

Max9296PrepareReport prepare_report = {};
gint prepare_ret =
    max9296_prepare_all(&prepare_input, &prepare_report, NULL);
if (prepare_ret < 0) {
  app_exit_code = EXIT_FAILURE;
  goto main_end;
}
```

Log generation, CSI, path, tuple, action, elapsed milliseconds, primary errno,
rollback errno, before/after state, and `LEGACY_NO_ABI` at high severity. Do not
cancel on normal shutdown.

- [ ] **Step 5: Wire Makefile targets**

Add `max9296Prepare.o` to `OBJS`, an object rule, and:

```make
$(OUTPUT)/testMax9296Prepare : test/test_max9296Prepare.cpp \
                              max9296Prepare.cpp max9296Prepare.h | $(OUTPUT)
	$(CXX) -Wall -Wextra $(CPP_PERF_FLAGS) -pthread -o $@ \
	  test/test_max9296Prepare.cpp max9296Prepare.cpp
```

- [ ] **Step 6: Run source, host, and cross-build gates**

```bash
bash test/run-max9296-prepare-source-check.sh
bash test/run-max9296-prepare-test.sh
./make-for-imx8 bin/testMax9296Prepare
./make-for-imx8 bin/gstApp
file bin/testMax9296Prepare bin/gstApp
```

Expected: both binaries are ARM aarch64; all tests pass.

- [ ] **Step 7: Commit Task 4**

```bash
git add main.cpp parser.h parser.cpp Makefile \
  test/run-max9296-prepare-source-check.sh \
  test/test_max9296Prepare.cpp
git commit -m "feat: prepare max9296 before gst pipeline start"
```

### Task 5: Full validation, graph review, and board handoff

**Files:**
- Modify if needed: `docs/superpowers/specs/2026-08-14-max9296-prepare-integration-design.md`
- Modify if needed: `docs/superpowers/plans/2026-08-14-max9296-prepare-integration.md`

**Interfaces:**
- Consumes: complete Tasks 1-4.
- Produces: reviewed branch and an exact board test stop point.

- [ ] **Step 1: Run all available host/static checks**

```bash
bash -n make-for-imx8 test/run-*.sh
bash test/run-make-for-imx8-test.sh
bash test/run-sync-config-source-check.sh
bash test/run-max9296-prepare-test.sh
bash test/run-max9296-prepare-source-check.sh
git diff --check HEAD~4..HEAD
```

- [ ] **Step 2: Rebuild both ARM64 artifacts from clean object state**

```bash
rm -f obj/max9296Prepare.o bin/testMax9296Prepare bin/gstApp
./make-for-imx8 bin/testMax9296Prepare bin/gstApp
file bin/testMax9296Prepare bin/gstApp
```

- [ ] **Step 3: Refresh the code-review graph and inspect impact**

Use `build_or_update_graph`, then `detect_changes`, `get_affected_flows`, and
`query_graph(pattern="tests_for")` for `max9296_prepare_all` and `main`.
Resolve every Critical/Important finding before continuing.

- [ ] **Step 4: Request independent spec and code review**

Review these invariants explicitly:

```text
lock precedes arg_parser hardware writes
prepare follows post-init enable mutation and precedes first state
warm reuse has lifetime gstApp ownership
new-vs-refreshed rollback provenance cannot cross
all admitted threads join; no async cancellation
first errno survives rollback failure
main reports prepare failure as non-zero process exit
```

- [ ] **Step 5: Stop before board deployment and hand off this target matrix**

Do not claim completion until the board executes:

1. cold dual-CSI: FW start/end intervals overlap, both READY use one epoch, and
   PAUSED/PLAYING causes no second FW load;
2. same-tuple restart after >3 minutes: no prepare write/reset/FW, cached
   controls apply, frames recover;
3. second gstApp: non-zero exit before enable/rotate/prepare writes; retry after
   first exit succeeds;
4. `play_delay >= 60`: PAUSED changes both to CONSUMED/lease0 before timeout;
5. single-left, single-right, and dual-wide on each CSI;
6. one missing ABI node, forced prepare error, and post-status failure rollback;
7. 100 restarts plus module unload/reload, checking power-count warnings,
   task leaks, and bounded unload.

- [ ] **Step 6: Commit review-only corrections if any**

```bash
git add -A
git commit -m "test: harden max9296 prepare integration"
```

Skip this commit when review requires no corrections.
