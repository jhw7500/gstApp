# RTSP Sync Configuration Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace temporary environment-variable controls for RTSP Frame ID SEI, synchronization tracing, and RTSP stall injection with the existing default → JSON → CLI configuration flow, while installing RTSP overrun instrumentation only when RTSP tracing is active.

**Architecture:** Add explicit fields to `CmdArg`, initialize them in `ParserClass::init_arg()`, load only the operational SEI switch from optional JSON, and let CLI values override JSON before `check_arg()` normalizes final state. Runtime components consume immutable `cmdArg` fields; tracing hooks are absent when duration is zero, while stall injection is an independently installed test-mode-only probe.

**Tech Stack:** C++, GLib/GOption, json-c, GStreamer 1.0, GStreamer RTSP Server, `gstreamer-codecparsers-1.0`, Makefile/Yocto i.MX8 cross compiler, Bash/SSH target validation.

## Global Constraints

- Work from `/home/jhw/ai/opencode/projects/gstApp` and prefix every shell command with `rtk`.
- Build only with `./make-for-imx8`; plain host `make` is invalid.
- Defaults: SEI `FALSE`, trace durations `0`, stall channel `-1`, stall times `0`.
- Precedence: compiled default → JSON → CLI → `check_arg()` normalization.
- JSON exposes only `rtsp_tune.frame_id_sei`; trace and stall remain CLI-only.
- Remove environment-variable fallback.
- Enable Frame ID SEI only for H.265; warn and disable an H.264 request.
- Stall requires `MODE_TEST`, an enabled valid channel, and complete valid times.
- With RTSP trace `0`, do not allocate `RtspSyncTrace`, install trace probes, find `rtsp_out_queue`, or connect its overrun callback.
- Do not add a build system, dependency, global, `system()`, or `popen()`.
- Match existing file formatting and use existing `__LOG` categories.
- The tree already contains synchronization instrumentation changes. Stage exact paths only; never use `git add -A`.
- Target server: `root@192.168.214.4`; external client: `root@192.168.214.8`. Every disruptive test must restore `cam-operate.service` and the original process state.

## File Map

| File | Responsibility |
| --- | --- |
| `cfgjson.h/.cpp`, `test/test_cfgjson.cpp` | Pure optional JSON boolean parsing and regression tests |
| `parser.h`, `util.h`, `parser.cpp` | Defaults, `CmdArg`, JSON/CLI parsing, normalization and logs |
| `videoBin.cpp`, `encoderBin.cpp`, `rtspServerBin.cpp` | Runtime consumption and conditional hook installation |
| `test/run-sync-config-cli-test.sh` | Cross-built binary CLI contract test |
| `test/run-sync-config-source-check.sh` | Structural gate for environment removal and callback guard |
| `tmp/run_rtsp_sei_resource_ab_host.sh` | Local target runner converted to CLI; not committed |
| `docs/CAMERA_SYNC_VALIDATION.md` | Historical evidence and current activation instructions |
| `docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md` | Product configuration contract |

---

### Task 1: Strict Optional JSON Boolean Accessor

**Files:**
- Modify: `cfgjson.h`
- Modify: `cfgjson.cpp`
- Modify: `test/test_cfgjson.cpp`

**Interfaces:**
- Produces: `CfgBoolStatus cfg_get_bool(json_object *obj, const char *name, gboolean *out)`.
- Guarantee: mutate `*out` only for JSON booleans or integer `0`/`1`.
- Consumed by: Task 2 `parser.cpp` JSON wrapper.

- [ ] **Step 1: Write failing tests**

Append cases to `test/test_cfgjson.cpp` for `true`, `false`, integer `1`, integer `0`, missing key, integer `2`, and string `"yes"`. Use this pattern for every case:

```cpp
{
  json_object *o = J("{\"frame_id_sei\":true}");
  gboolean out = FALSE;
  CHECK(cfg_get_bool(o, "frame_id_sei", &out) == CFG_BOOL_OK);
  CHECK(out == TRUE);
  json_object_put(o);
}
```

For missing/bad cases, initialize `out` to a sentinel value and assert it remains unchanged. Expected statuses are `CFG_BOOL_MISSING`, `CFG_BOOL_BAD_VALUE`, and `CFG_BOOL_BAD_TYPE` respectively.

- [ ] **Step 2: Verify the new API is missing**

```bash
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-cfgjson-test.sh
```

Expected: cross compilation fails on `CfgBoolStatus`, `cfg_get_bool`, or `CFG_BOOL_*`.

- [ ] **Step 3: Implement the pure accessor**

Add to `cfgjson.h`:

```cpp
typedef enum {
  CFG_BOOL_OK = 0,
  CFG_BOOL_MISSING,
  CFG_BOOL_BAD_TYPE,
  CFG_BOOL_BAD_VALUE
} CfgBoolStatus;

CfgBoolStatus cfg_get_bool(json_object *obj, const char *name, gboolean *out);
```

Add to `cfgjson.cpp`:

```cpp
CfgBoolStatus cfg_get_bool(json_object *obj, const char *name, gboolean *out) {
  if (!obj || !name || !out)
    return CFG_BOOL_MISSING;
  json_object *value = json_object_object_get(obj, name);
  if (!value)
    return CFG_BOOL_MISSING;
  enum json_type type = json_object_get_type(value);
  if (type == json_type_boolean) {
    *out = json_object_get_boolean(value) ? TRUE : FALSE;
    return CFG_BOOL_OK;
  }
  if (type != json_type_int)
    return CFG_BOOL_BAD_TYPE;
  gint number = json_object_get_int(value);
  if (number != 0 && number != 1)
    return CFG_BOOL_BAD_VALUE;
  *out = number == 1 ? TRUE : FALSE;
  return CFG_BOOL_OK;
}
```

- [ ] **Step 4: Run target tests**

```bash
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-cfgjson-test.sh
```

Expected: all old array checks and seven new boolean cases pass with zero failures.

- [ ] **Step 5: Commit**

```bash
rtk git add cfgjson.h cfgjson.cpp test/test_cfgjson.cpp
rtk git diff --cached --check
rtk git commit -m "test: cover optional JSON boolean settings"
```

---

### Task 2: CmdArg, JSON, CLI, and Normalization

**Files:**
- Modify: `parser.h`
- Modify: `util.h`
- Modify: `parser.cpp`
- Create: `test/run-sync-config-cli-test.sh`

**Interfaces:**
- Consumes: `cfg_get_bool()` from Task 1.
- Produces `CmdArg` fields: `rtsp_frame_id_sei`, `v4l2_sync_trace_sec`, `channel_sync_trace_sec`, `rtsp_sync_trace_sec`, `rtsp_test_stall_ch`, `rtsp_test_stall_after_sec`, `rtsp_test_stall_duration_sec`.
- Produces CLI names: `--rtsp-frame-id-sei`, `--v4l2-sync-trace-sec`, `--channel-sync-trace-sec`, `--rtsp-sync-trace-sec`, `--rtsp-test-stall-ch`, `--rtsp-test-stall-after-sec`, `--rtsp-test-stall-duration-sec`.

- [ ] **Step 1: Write a failing CLI contract**

Create `test/run-sync-config-cli-test.sh`:

```bash
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${BOARD:=192.168.214.4}"
: "${BOARD_PW:?BOARD_PW must be set}"
: "${BIN:=bin/gstApp}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o ConnectTimeout=8)
REMOTE=/tmp/gstApp-sync-config-cli-test
base64 "$BIN" | sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "base64 -d > '$REMOTE' && chmod 755 '$REMOTE'"
help=$(sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" \
  "'$REMOTE' --help-all")
for option in \
  --rtsp-frame-id-sei --v4l2-sync-trace-sec \
  --channel-sync-trace-sec --rtsp-sync-trace-sec \
  --rtsp-test-stall-ch --rtsp-test-stall-after-sec \
  --rtsp-test-stall-duration-sec
do
  grep -q -- "$option" <<<"$help"
done
sshpass -p "$BOARD_PW" ssh "${SSH_OPTS[@]}" root@"$BOARD" "rm -f '$REMOTE'"
echo "sync config CLI contract: PASSED"
```

- [ ] **Step 2: Prove the old binary lacks the options**

```bash
rtk chmod +x test/run-sync-config-cli-test.sh
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-sync-config-cli-test.sh
```

Expected: nonzero exit on a missing option.

- [ ] **Step 3: Add defaults and fields**

Add to `parser.h`:

```cpp
#define DEFAULT_RTSP_FRAME_ID_SEI FALSE
#define DEFAULT_V4L2_SYNC_TRACE_SEC 0
#define DEFAULT_CHANNEL_SYNC_TRACE_SEC 0
#define DEFAULT_RTSP_SYNC_TRACE_SEC 0
#define SYNC_TRACE_MAX_SEC 3600
#define DEFAULT_RTSP_TEST_STALL_CH (-1)
#define DEFAULT_RTSP_TEST_STALL_AFTER_SEC 0
#define DEFAULT_RTSP_TEST_STALL_DURATION_SEC 0
#define RTSP_TEST_STALL_MAX_SEC 3600
```

Add the seven fields from **Interfaces** next to RTSP tuning fields in `CmdArg` (`util.h`). Initialize every field from its default in `ParserClass::init_arg()`.

- [ ] **Step 4: Parse optional JSON**

Add a `parser.cpp` wrapper around `cfg_get_bool()`:

```cpp
static void json_object_get_bool_optional(json_object *obj,
                                          const gchar *name,
                                          gboolean *out) {
  CfgBoolStatus status = cfg_get_bool(obj, name, out);
  if (status == CFG_BOOL_OK) {
    __LOG(LOG_INFO, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, name,
          *out ? "true" : "false");
  } else if (status != CFG_BOOL_MISSING) {
    __LOG(LOG_ERR,
          "[CFG][%s:%d] %s must be boolean or integer 0/1; keep default %s",
          _FILE_, __LINE__, name, *out ? "true" : "false");
    g_cfg_errors++;
  }
}
```

Inside the optional `rtsp_tune` block call:

```cpp
json_object_get_bool_optional(tune_obj, "frame_id_sei",
                              &arg.rtsp_frame_id_sei);
```

- [ ] **Step 5: Register CLI options**

Add seven `GOptionEntry` records using `G_OPTION_ARG_INT` and the exact names above. Store into matching `arg` fields so CLI naturally overrides JSON. Help must state: SEI `0=off, 1=on`; trace `0=off, 1..3600=seconds`; stall requires `--test=1`.

- [ ] **Step 6: Normalize final values in `check_arg()`**

Add:

```cpp
static void sync_trace_sanity(gint *value, const gchar *name) {
  if (*value >= 0 && *value <= SYNC_TRACE_MAX_SEC)
    return;
  __LOG(LOG_WARNING,
        "[CFG][%s:%d] invalid %s=%d (valid 0..%d), disabling",
        _FILE_, __LINE__, name, *value, SYNC_TRACE_MAX_SEC);
  *value = 0;
}
```

After codec validation:

```cpp
if (arg.rtsp_frame_id_sei != FALSE && arg.rtsp_frame_id_sei != TRUE) {
  __LOG(LOG_WARNING,
        "[CFG][%s:%d] invalid rtsp_frame_id_sei=%d, disabling",
        _FILE_, __LINE__, arg.rtsp_frame_id_sei);
  arg.rtsp_frame_id_sei = FALSE;
}
if (arg.rtsp_frame_id_sei && !use_h265) {
  __LOG(LOG_WARNING,
        "[CFG][%s:%d] RTSP Frame ID SEI requires H.265, disabling",
        _FILE_, __LINE__);
  arg.rtsp_frame_id_sei = FALSE;
}
sync_trace_sanity(&arg.v4l2_sync_trace_sec, "v4l2_sync_trace_sec");
sync_trace_sanity(&arg.channel_sync_trace_sec, "channel_sync_trace_sec");
sync_trace_sanity(&arg.rtsp_sync_trace_sec, "rtsp_sync_trace_sec");
```

Treat stall as requested when any stall field differs from default. Keep it only when `levelMode == MODE_TEST`, channel is `0..3`, its bit is enabled in `ch_enable`, `after_sec` is `0..3600`, and `duration_sec` is `1..3600`. Otherwise log once and restore all three stall defaults.

- [ ] **Step 7: Log effective settings**

Extend `rtsp_tune` logging with `frame_id_sei`. Add one post-normalization line containing all trace durations and stall fields. Never log the raw pre-normalized values as effective settings.

- [ ] **Step 8: Build and pass CLI contract**

```bash
rtk ./make-for-imx8 -j4
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-sync-config-cli-test.sh
```

Expected: build succeeds and script prints `sync config CLI contract: PASSED` without stopping the service.

- [ ] **Step 9: Commit**

```bash
rtk git add parser.h util.h parser.cpp test/run-sync-config-cli-test.sh
rtk git diff --cached --check
rtk git commit -m "feat: configure RTSP synchronization through JSON and CLI"
```

---

### Task 3: Runtime Migration and Conditional Hooks

**Files:**
- Modify: `videoBin.cpp`
- Modify: `encoderBin.cpp`
- Modify: `rtspServerBin.cpp`
- Create: `test/run-sync-config-source-check.sh`

**Interfaces:**
- Consumes: seven normalized `cmdArg` fields from Task 2.
- Preserves: Frame ID payload, trace counters/probe positions, queue limits, PTS stripping, appsrc timestamps, and stall duration behavior.
- Changes: overrun hookup is conditional; stall no longer depends on RTSP trace.

- [ ] **Step 1: Write a failing structural gate**

Create `test/run-sync-config-source-check.sh`:

```bash
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if grep -nE 'GSTAPP_(V4L2_SYNC_TRACE_SEC|CHANNEL_SYNC_TRACE_SEC|RTSP_SYNC_TRACE_SEC|RTSP_FRAME_ID_SEI|RTSP_TEST_STALL)' \
    videoBin.cpp encoderBin.cpp rtspServerBin.cpp; then
  echo "legacy sync environment control remains" >&2
  exit 1
fi
grep -q 'cmdArg.v4l2_sync_trace_sec' videoBin.cpp
grep -q 'cmdArg.channel_sync_trace_sec' encoderBin.cpp
grep -q 'cmdArg.rtsp_sync_trace_sec' rtspServerBin.cpp
grep -q 'cmdArg.rtsp_frame_id_sei' rtspServerBin.cpp
grep -q 'cmdArg.rtsp_test_stall_ch' rtspServerBin.cpp
python3 - <<'PY'
from pathlib import Path
s = Path('rtspServerBin.cpp').read_text(encoding='utf-8')
needle = 'gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_out_queue")'
pos = s.index(needle)
if 'if (info->sync_trace)' not in s[max(0, pos - 500):pos]:
    raise SystemExit('rtsp_out_queue lookup is not guarded by sync_trace')
print('sync config source contract: PASSED')
PY
```

- [ ] **Step 2: Verify it fails on current environment controls**

```bash
rtk chmod +x test/run-sync-config-source-check.sh
rtk bash test/run-sync-config-source-check.sh
```

Expected: failure listing `GSTAPP_*` definitions.

- [ ] **Step 3: Migrate V4L2 trace**

Remove the V4L2 environment macro/parsing and start `install_v4l2_sync_trace()` with:

```cpp
static void install_v4l2_sync_trace(GstElement *src, guint8 csi) {
  guint duration_sec = (guint)cmdArg.v4l2_sync_trace_sec;
  if (duration_sec == 0)
    return;
```

Keep existing pad lookup, allocation, probe, summary, and destroy callback.

- [ ] **Step 4: Migrate channel trace**

Remove `CHANNEL_SYNC_TRACE_ENV` and replace its cache parser with:

```cpp
static guint channel_sync_trace_duration(void)
{
    return cmdArg.channel_sync_trace_sec > 0
               ? (guint)cmdArg.channel_sync_trace_sec
               : 0;
}
```

Retain callers' zero-duration returns so disabled mode does not look up pads or allocate state.

- [ ] **Step 5: Migrate RTSP settings**

In `rtspServerBin.cpp`:

1. Remove all five synchronization environment macros and associated parsing.
2. Return normalized `cmdArg.rtsp_sync_trace_sec` from `rtsp_sync_trace_duration()`.
3. Return `cmdArg.rtsp_frame_id_sei` from `rtsp_frame_id_sei_enabled()` without a static cache.
4. Make `rtsp_test_stall_config()` match `cmdArg.rtsp_test_stall_ch` to `ch` and copy normalized after/duration values.
5. Still return `FALSE` for a channel mismatch or zero duration as defense in depth.

- [ ] **Step 6: Separate trace and stall probe installation**

Refactor the media probe helper to implement exactly:

```text
trace != NULL:
  install appsrc src buffer trace
  install pay0 sink buffer trace
  install pay0 src RTP trace

stall matches channel:
  install pay0 src stall probe
```

Return immediately only when both trace is NULL and stall does not match. Locate `pay0` when either path needs it. Preserve all context free callbacks and object unrefs.

- [ ] **Step 7: Guard overrun hookup**

Wrap queue lookup, missing-queue log, signal connection and unref in `media_configure()`:

```cpp
if (info->sync_trace) {
  out_queue =
      gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_out_queue");
  if (out_queue) {
    g_signal_connect(out_queue, "overrun",
                     (GCallback)rtsp_factory_queue_overrun, info);
    gst_object_unref(out_queue);
  } else {
    __LOG(LOG_ERR, "[RTSP_SYNC][%s:%d] ch=%u rtsp_out_queue not found",
          _FILE_, __LINE__, info->ch);
  }
}
```

Keep the queue name and queue behavior unchanged. Add one low-frequency notice when the callback is connected so target validation can prove installation; do not log per overrun unless already implemented.

- [ ] **Step 8: Run gates**

```bash
rtk bash test/run-sync-config-source-check.sh
rtk ./make-for-imx8 -j4
rtk git diff --check
```

Expected: source contract and cross-build pass with no legacy sync environment references in runtime files.

- [ ] **Step 9: Commit**

```bash
rtk git add videoBin.cpp encoderBin.cpp rtspServerBin.cpp \
  test/run-sync-config-source-check.sh
rtk git diff --cached --check
rtk git commit -m "refactor: isolate optional RTSP synchronization hooks"
```

---

### Task 4: Runner and Korean Documentation Migration

**Files:**
- Modify: `tmp/run_rtsp_sei_resource_ab_host.sh` (local artifact; do not commit)
- Modify: `docs/CAMERA_SYNC_VALIDATION.md`
- Modify: `docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md`

**Interfaces:**
- Consumes: CLI names from Task 2.
- Produces: current operator instructions and an A/B runner with no synchronization environment injection.

- [ ] **Step 1: Convert the A/B runner to manual CLI execution**

Stop replacing `/usr/local/bin/gstApp`. After stopping `cam-operate.service` and waiting for `gstApp`/`killcam` to exit, run the `/tmp` test binary from `/root` with the normal command and explicit settings:

```bash
cd /root
nohup "$TEST_APP" -d 22 -m 4 \
  --rtsp-sync-trace-sec="$TRACE_SEC" \
  --rtsp-frame-id-sei="$sei" \
  >"${REMOTE_PREFIX}_${label}_gstapp.log" 2>&1 &
echo \$! >"${REMOTE_PREFIX}_${label}_gstapp_pid.log"
```

Readiness must inspect `/proc/$pid/cmdline`, not `/proc/$pid/environ`. Phase cleanup must terminate only the recorded manual PID and wait for exit. The EXIT trap must restart `cam-operate.service`.

- [ ] **Step 2: Remove environment cleanup/assertions**

Delete synchronization `systemctl set-environment`/`unset-environment` calls. Replace `TEST_ENV_COUNT` with assertions that the restored process command is `gstApp -d 22 -m 4` and no manual PID remains.

- [ ] **Step 3: Update historical and current documentation**

In `docs/CAMERA_SYNC_VALIDATION.md`, add a dated note saying old `GSTAPP_*` commands are retained as historical measurement inputs. Add a current mapping table to the seven CLI names and state product SEI can also be set with `rtsp_tune.frame_id_sei`. Change only current methodology/next-step examples; do not rewrite historical measurements.

In `docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md`, document default OFF, default → JSON → CLI precedence, JSON/CLI SEI controls, CLI-only traces, test-mode-only stall, and conditional overrun hookup. Preserve all measured CPU/PSS/network/timing/spread values.

- [ ] **Step 4: Verify runner and docs**

```bash
rtk bash -n tmp/run_rtsp_sei_resource_ab_host.sh
rtk grep -n "rtsp-frame-id-sei\|frame_id_sei\|rtsp-sync-trace-sec" \
  docs/CAMERA_SYNC_VALIDATION.md docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md
rtk git diff --check -- docs/CAMERA_SYNC_VALIDATION.md \
  docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md
```

- [ ] **Step 5: Commit product docs only**

```bash
rtk git add docs/CAMERA_SYNC_VALIDATION.md docs/RTSP_CLIENT_SYNC_REQUIREMENTS.md
rtk git diff --cached --check
rtk git commit -m "docs: document RTSP synchronization configuration"
```

Do not stage the `tmp/` runner.

---

### Task 5: Local and Non-Disruptive Target Gates

**Files:**
- Verify: all implementation/test files from Tasks 1–4

**Interfaces:**
- Produces: build, source, JSON helper, CLI, and pre-integration service-state evidence.

- [ ] **Step 1: Run syntax and structural gates**

```bash
rtk bash -n test/run-cfgjson-test.sh
rtk bash -n test/run-sync-config-cli-test.sh
rtk bash -n test/run-sync-config-source-check.sh
rtk bash test/run-sync-config-source-check.sh
rtk git diff --check
```

- [ ] **Step 2: Perform a clean cross-build**

```bash
rtk ./make-for-imx8 clean
rtk ./make-for-imx8 -j4
rtk ./make-for-imx8 -j4 bin/rtspFrameSyncClient bin/decoderRecoveryClient
```

Expected: `bin/gstApp` and both target-side validation clients build; record existing warnings separately and reject new warnings attributable to this change.

- [ ] **Step 3: Run JSON and CLI target contracts**

```bash
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-cfgjson-test.sh
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-sync-config-cli-test.sh
```

Expected: JSON checks report zero failures; all seven options appear; service stays active.

- [ ] **Step 4: Capture pre-test service state**

```bash
rtk sshpass -p root ssh -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null root@192.168.214.4 \
  'systemctl is-active cam-operate.service; pgrep -a gstApp; sha256sum /usr/local/bin/gstApp'
```

Expected: active service, one `gstApp -d 22 -m 4` process, and recorded production SHA.

---

### Task 6: OFF/ON/Stall Smoke and Restoration

**Files:**
- Verify: `bin/gstApp`
- Verify: `bin/rtspFrameSyncClient`
- Modify: `docs/CAMERA_SYNC_VALIDATION.md`
- Generate: `tmp/target-192.168.214.4/rtsp_sync_config_<timestamp>/`

**Interfaces:**
- Consumes: Task 4 manual runner and Task 5 binaries.
- Produces: default behavior, precedence, trace, stall, client synchronization and restoration evidence.

- [ ] **Step 1: Deploy only to `/tmp` and verify SHA**

Copy server binary to `/tmp/gstApp.sync-config-test` and client to `/tmp/rtspFrameSyncClient.sync-config-test`. Require local/remote SHA equality before stopping the service. Never overwrite `/usr/local/bin/gstApp`.

- [ ] **Step 2: Run default-OFF smoke for at least 60 seconds**

Stop the service, wait for `gstApp` and `killcam` to disappear, then run `/tmp/gstApp.sync-config-test -d 22 -m 4` from `/root`. Require four-channel reception, no sync trace/SEI/stall activation log, no Frame ID barrier, zero push failures, and a live server process.

- [ ] **Step 3: Verify invalid and codec-incompatible CLI normalization**

Run long enough to capture the effective configuration log:

```bash
/tmp/gstApp.sync-config-test -d 22 -m 4 --enc=h264 \
  --rtsp-frame-id-sei=1 \
  --v4l2-sync-trace-sec=-1 \
  --channel-sync-trace-sec=3601 \
  --rtsp-sync-trace-sec=3601 \
  --rtsp-test-stall-ch=4 \
  --rtsp-test-stall-after-sec=0 \
  --rtsp-test-stall-duration-sec=10
```

Require one warning per invalid category and effective values SEI `0`, all traces `0`, stall channel `-1`, and stall times `0`. Require no trace/stall probe activation log.

- [ ] **Step 4: Verify invalid JSON, JSON ON, then CLI OFF precedence**

Back up `/root/shared_v/edgeconf_pim.json` to a run-ID-specific file. Write each test value atomically:

```bash
jq '.VHL_CAM.rtsp_tune.frame_id_sei="yes"' \
  /root/shared_v/edgeconf_pim.json > /root/shared_v/edgeconf_pim.json.sync.tmp
mv /root/shared_v/edgeconf_pim.json.sync.tmp /root/shared_v/edgeconf_pim.json
```

Require one config error and effective SEI OFF. Then repeat with
`.VHL_CAM.rtsp_tune.frame_id_sei=true`, run without CLI override, and require a Frame ID barrier with zero mismatch for 60 seconds. Restart with:

```bash
/tmp/gstApp.sync-config-test -d 22 -m 4 --rtsp-frame-id-sei=0
```

Require effective SEI OFF and no barrier. Restore edgeconf immediately, including through the EXIT trap.

- [ ] **Step 5: Verify traces and conditional overrun hookup**

```bash
/tmp/gstApp.sync-config-test -d 22 -m 4 \
  --v4l2-sync-trace-sec=20 \
  --channel-sync-trace-sec=20 \
  --rtsp-sync-trace-sec=20 \
  --rtsp-frame-id-sei=1
```

Require all three summary categories after 20 seconds, callback-connection notices only in this phase, four-channel Frame ID groups, zero mismatch, and zero insertion/push failures.

- [ ] **Step 6: Verify test-mode-only stall**

Run stall CLI once without `--test=1`; require one normalization warning and no stall. Then run:

```bash
/tmp/gstApp.sync-config-test -d 22 -m 4 --test=1 \
  --rtsp-frame-id-sei=1 \
  --rtsp-test-stall-ch=2 \
  --rtsp-test-stall-after-sec=7 \
  --rtsp-test-stall-duration-sec=10
```

Require stall start/end only on channel 2 and client restoration of a common Frame ID barrier without cross-ID grouping.

- [ ] **Step 7: Restore production state**

The EXIT trap must stop the manual PID, restore edgeconf, copy evidence locally, remove remote test artifacts, and start `cam-operate.service`. Within 120 seconds require:

```text
service active
production file/process SHA equals pre-test SHA
command is gstApp -d 22 -m 4
open /dev/video* FDs >= 4
RTSP port 8554 listening
manual test process count = 0
temporary edgeconf backup count = 0
```

- [ ] **Step 8: Record Korean results**

Append commands, SHAs, OFF/JSON ON/CLI OFF/trace/stall results, client counters and restoration evidence to `docs/CAMERA_SYNC_VALIDATION.md`. Label measured facts and inferences separately.

- [ ] **Step 9: Run final gates and commit result**

```bash
rtk ./make-for-imx8 -j4
rtk bash test/run-sync-config-source-check.sh
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-cfgjson-test.sh
rtk env BOARD=192.168.214.4 BOARD_PW=root bash test/run-sync-config-cli-test.sh
rtk git diff --check
rtk git status --short
```

After confirming no unrelated file is staged:

```bash
rtk git add docs/CAMERA_SYNC_VALIDATION.md
rtk git diff --cached --check
rtk git commit -m "test: validate RTSP synchronization configuration"
```

Stop only when every gate passes, restoration evidence is complete, and only known unrelated working-tree files remain uncommitted.
