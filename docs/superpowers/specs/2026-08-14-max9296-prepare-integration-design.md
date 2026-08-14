# gstApp MAX9296 Parallel Prepare Integration Design

## Scope

This change makes `gstApp` consume the MAX9296 parallel-prepare v1 sysfs ABI.
It does not change camera recovery policy, JSON snapshot policy, process
supervision, the existing channel-diagnostic work from PR #38, or the
MAX9296 driver.

The integration has three goals:

1. Cold startup prepares the two independent CSI domains concurrently instead
   of letting the i.MX8 media graph serialize their blocking `s_stream(1)`
   calls.
2. A same-configuration warm restart reuses retained firmware without issuing
   an unconditional prepare that would fail with `EBUSY` while the driver's
   V4L2 power owner remains accounted.
3. Any ambiguous, partially available, or stale state fails before the first
   GStreamer state transition rather than starting a partial camera graph.

One MAX9296 instance remains one indivisible stream domain. A dual-wide CSI is
never split into two prepare operations.

## Considered approaches

### 1. External launcher or PIM shell helper

This can run the two sysfs writes before `exec gstApp`, but it must duplicate
the final CLI-overridden channel tuple, carry the generation and result across
the process boundary, and keep launch delay below the lease window. It also
makes runtime ownership and rollback harder to test. This is not selected.

### 2. Parallel GStreamer child state changes

This retains the existing V4L2 API only, but the i.MX8 media graph owns a
shared graph mutex while invoking blocking subdevice stream operations. Child
threads therefore do not provide a reliable independent hardware prepare
boundary. This is not selected.

### 3. Internal sysfs prepare coordinator

`gstApp` already owns the fully resolved JSON plus CLI configuration and knows
which CSI domains it will create. A small GStreamer-independent coordinator
can validate, prepare, and roll back both domains before the first pipeline
state transition. This is the selected approach.

## Physical and software mapping

The current target mapping is:

| gstApp domain | Logical channels | MAX9296 prepare node |
| --- | --- | --- |
| CSI0 | ch0/ch1 | `/sys/bus/i2c/devices/2-0048/prepare` |
| CSI1 | ch2/ch3 | `/sys/bus/i2c/devices/1-0048/prepare` |

For each active CSI, derive the driver tuple from the exact runtime values
that `VideoBin` will use:

```text
enable = (left_enabled ? 1 : 0) | (right_enabled ? 2 : 0)
width  = cmdArg.width * (enable == 3 ? 2 : 1)
height = cmdArg.height
fps    = cmdArg.main_fps[csi]
code   = 0x2006 (driver UYVY contract)
```

The two domains share one physical FSYNC. If both are active, their FPS must
match before any sysfs write occurs. Width, height, and channel mask remain
per-domain.

The existing parser has confusing CSI-index assignments around
`parser.cpp:1023-1026`. This integration deliberately consumes
`cmdArg.main_fps[csi]`, the same value that `VideoBin` installs in source caps,
and does not bundle an unrelated parser correction.

## Component boundary

Add a standalone `max9296Prepare.h/.cpp` module with no GStreamer, JSON, or
`util.h` dependency. It owns:

- request construction and tuple validation;
- strict parsing of the v1 `key=value` status line;
- cold/warm/stale policy;
- a narrow lifetime-held camera-owner advisory lock for gstApp processes;
- concurrent blocking writes for independent CSI domains;
- post-write status validation;
- cancellation of only the unused leases created by the current transaction;
- an injectable POSIX I/O interface for host tests.

The module must compile with the production `-fno-exceptions -fno-rtti`
flags. It uses fixed-size value types and `pthread` workers; it does not create
Glib objects or write host-test binaries into the target `bin/` directory.

`main.cpp` owns only the conversion from final `cmdArg` to coordinator input,
holding the returned owner-lock descriptor for process lifetime, logging the
report, and deciding whether startup continues.

The coordinator returns original negative errno values. It never converts
`EBUSY` into success and never decides which reset action should follow an
`ESTALE` result.

## Startup data flow

Split the current `ParserClass::arg_parser()` hardware side effects into a new
`apply_camera_sysfs()` method. JSON parsing, CLI parsing, `check_arg()`, and the
final `cmdArg` copy become pure with respect to MAX9296. Build and validate the
requested prepare tuples before any enable/rotate/prepare write, so an invalid
launch cannot turn a retained READY/CONSUMED state into STALE.

After pure parsing and validation, every gstApp process acquires a non-blocking
exclusive flock on `/run/lock/gstapp-camera.lock` with an `O_CLOEXEC` descriptor
and keeps it open until process cleanup. Only `EWOULDBLOCK` means another
gstApp owner; open, permission, and descriptor failures preserve their own
errno. The minimal implementation also locks an audio-only gstApp. After the
lock is held, `apply_camera_sysfs()` writes enable/rotate, then VideoBin applies
cached controls. The lock does not cover or serialize `init_cam.sh`,
`kill_test.sh`, module reset, or hard reset; killing the owning gstApp releases
it automatically.

The startup order is therefore fixed as:

```text
JSON parse -> CLI parse -> check/final cmdArg -> pure tuple/FPS validation
-> gstApp owner flock -> enable/rotate sysfs apply -> VideoBin/control setup
-> parallel prepare -> first PAUSED/PLAYING transition
```

Run the prepare coordinator after the channel loop has constructed pipeline
bins, applied cached subdevice controls, and performed its CSI-init failure
mask updates, but immediately before the first `PAUSED` or `PLAYING` state
transition. This creates a short prepare-to-V4L2 handoff interval, uses the
post-init enable mask, and remains valid when `play_delay` causes `PAUSED` to be
the first transition.

1. Build one target for each active CSI and validate all tuples and shared FPS.
   If no CSI is active, return a successful no-op without touching sysfs.
2. Probe every active prepare node.
   - If all active nodes are absent with `ENOENT`, log `LEGACY_NO_ABI` and use
     the existing V4L2 initialization path.
   - If only a subset is absent, or a present node returns another I/O error,
     fail closed.
3. Read and parse every status.
4. A domain is eligible for warm reuse only when all of the following hold:
   - `state=CONSUMED`;
   - `lease=0`;
   - `match=1`;
   - `worker_errno=0`;
   - the retained prepare generation is non-zero;
   - epoch is non-zero;
   - mode, table, width, height, FPS, enable mask, and code exactly match the
     requested tuple.
5. A `CONSUMED/lease=0` domain that does not satisfy the warm predicate but has
   `worker_errno=0` is submitted as a new prepare request. This supports the
   valid cold case where a balanced `s_power(0)` advanced the hardware epoch
   but the diagnostic state name remained CONSUMED. The driver remains the
   authority: a retained V4L2 owner returns `EBUSY`, and an unsafe same-epoch
   tuple change returns `ESTALE`; both are surfaced as reset-required failure.
6. An exact, valid pre-existing `READY/lease=1` domain is refreshed with its
   original non-zero generation. It is marked as pre-existing ownership and is
   never cancelled by this transaction. A READY state with a mismatched tuple,
   invalid generation, `match=0`, or non-zero worker error fails closed.
7. `IDLE`, `FAILED`, `EXPIRED`, `STALE`, and the worker-clean non-warm
   `CONSUMED` states from step 5 without a lease are prepared with the same new,
   non-zero orchestration generation. A non-zero worker error fails closed
   without a write.
   Domains needing work issue their blocking writes concurrently. Inconsistent
   combinations such as READY without a lease or any non-READY state with a
   lease fail closed rather than being normalized by userspace.
8. After each successful write, require:
   - `state=READY` and generation equal to either this transaction for a new
     lease or the captured original generation for a refreshed lease;
   - the expected mode/table, exact tuple, and `code=0x2006`;
   - `errno=0`, `worker_errno=0`, `lease=1`, and `match=1`.
9. Re-read every active status after all workers join, including warm domains,
   then require the expected state/fingerprint and the same non-zero hardware
   epoch across all active domains.
10. A new-lease domain becomes rollback-owned as soon as its complete sysfs
    write succeeds. If any peer write or any final status read/parse/validation
    fails, write `0` to every rollback-owned domain even when its final status
    could not be read. Never cancel a warm CONSUMED owner or a refreshed lease
    that predated this transaction.
11. Only a complete success or explicit all-node legacy fallback may proceed
    to the first GStreamer state transition.

The transaction generation is derived from a monotonic clock plus the process
identity and is forced non-zero. `EAGAIN` is retried at most three times with a
100 ms delay for each individual read or write operation. An unsupported
status state, malformed line, duplicate/missing required field, numeric
overflow, partial node absence, short write, or post-write mismatch fails
closed. Parsing is syntactic: an IDLE status may legitimately contain
`mode=none table=none` and zero tuple fields; request-specific tuple validation
belongs to state policy, not the status parser.

A mixed transaction is valid: one CSI may be an exact warm CONSUMED owner or a
refreshed pre-existing READY lease while the other is newly prepared. The final
same-epoch check still applies, and a failure rolls back only the newly created
READY lease.

`prepare errno` is a stored request diagnostic. A warm CONSUMED state may
retain an older rejected-request errno, so warm validity uses the current
fingerprint and worker gate and logs a non-zero stored errno rather than
silently treating it as current hardware failure. The same applies to an exact
pre-existing READY lease: its pre-status errno is diagnostic only, and a
successful original-generation refresh must clear it to zero in post-status.

## Ownership and lifecycle

The first V4L2 `s_power(1)` consumes a READY lease. Normal gstApp shutdown must
not issue sysfs cancel because ownership has already transferred to V4L2.

Board evidence shows that current GStreamer/vendor-media shutdown invokes
`s_stream(0)` but not `s_power(0)`. A later same-tuple process therefore sees a
CONSUMED warm candidate. The prepare status does not expose PID, streaming, or
power-count ownership, so the lifetime owner lock is the local evidence that no
second gstApp can make the same warm decision. Higher-level ownership remains
responsible for launch/restart policy; this lock only rejects duplicate gstApp
camera owners and does not prevent manual hardware maintenance commands.

The sysfs write itself is synchronous through register-table and firmware
programming. Userspace cannot safely impose a hard cancellation timeout on an
in-flight store. The coordinator records elapsed time and joins every admitted
write. A true hard timeout requires a future asynchronous/cancellable driver
ABI. The driver's 60-second timer is a post-success unused-lease timeout, not
a firmware-write timeout.

Signals are not used to asynchronously cancel an admitted sysfs write. The
coordinator joins both workers and reports elapsed monotonic time. If external
termination kills the process before the coordinator can finish, only a
successfully published but unconsumed lease exists and the driver reclaims it
through that 60-second timeout.

If the process is terminated before V4L2 consumes a newly prepared lease, the
driver's unused-lease timeout remains the final cleanup guarantee. A partial
failure observed by the coordinator is cancelled immediately as described
above.

## Error and rollout policy

| Condition | gstApp result |
| --- | --- |
| All active prepare nodes absent | high-severity legacy fallback |
| Partial node absence | fail closed |
| Invalid app tuple/shared FPS mismatch | fail before any write |
| `PREPARING`/release race or write `EBUSY` | fail; never infer success |
| CONSUMED exact current tuple | warm reuse while the gstApp owner flock is held |
| Non-warm CONSUMED, worker clean | submit prepare; propagate driver success, `EBUSY`, or `ESTALE` |
| Any CONSUMED worker error | fail closed without a write |
| Write/status `ESTALE` | fail with reset-required diagnosis |
| `EAGAIN` during the short probe-commit window | bounded retry |
| Hardware/FW/I/O error | preserve errno and fail |
| One new READY and one failure | cancel only the new READY lease, then fail |
| Second gstApp camera owner | fail before subdevice-control writes |

Automatic all-node fallback keeps one binary compatible with the old driver
during canary rollout. Once packaging guarantees the v1 ABI, deployment may
make missing nodes fatal without changing the coordinator's hardware policy.

## Testing

### Host behavior tests

A standalone test binary with an injected fake I/O backend verifies:

- HD/FHD dual and left/right single tuple construction;
- disabled CSI handling and current bus mapping;
- invalid dimensions, masks, generation, FPS range, and shared-FPS mismatch;
- status parsing with reordered/extra keys, missing/duplicate fields,
  overflow, negative errno, and unsupported states;
- exact CONSUMED warm reuse with no write;
- exact pre-existing READY refresh with its original generation and preserved
  rollback provenance;
- owner-lock contention and release-on-close behavior;
- two writes entering a barrier concurrently and sharing one generation;
- post-status generation/tuple/epoch/lease/worker validation;
- original errno propagation, short writes, and bounded `EAGAIN` retry;
- partial-success rollback without cancelling warm or pre-existing leases;
- full new-lease write followed by final status-read failure, including cancel
  failure that preserves the original transaction errno in the result;
- READY with stored `errno=-ESTALE` refreshing to errno zero, and fail-closed
  handling for STALE/lease=1 and READY/lease=0;
- second-worker creation failure joining the admitted worker and rolling back
  any new lease it published;
- all-missing legacy fallback versus partial-missing failure.

A source-order test verifies pure JSON/CLI/check/final-copy ordering, tuple
validation before any MAX9296 write, owner-lock acquisition, exactly one
`apply_camera_sysfs()` call after the lock and before `VideoBin::init()`, then
the coordinator after the channel loop's enable-mask mutation and before either
`GST_STATE_PAUSED` or `GST_STATE_PLAYING`. `arg_parser()` must contain no
enable/rotate write. Prepare failure must set a non-zero process exit status
before `main_end`; success and legacy fallback preserve the existing zero exit.
The Makefile must build the module into both gstApp and the cross-compiled
target test.

### Build and target gates

- Existing shell syntax, wrapper, and source-contract tests remain green.
- Host standalone prepare tests pass without GStreamer development packages.
- The Yocto SDK cross-build produces `gstApp` and the prepare test binary.
- Cold target startup shows overlapping firmware intervals and no second
  firmware download at GStreamer state transition; both READY leases become
  CONSUMED with the same non-zero epoch.
- Same-tuple restart after more than three minutes performs no prepare write,
  reset, or firmware download.
- Invalid tuple/FPS exits before enable/rotate/prepare writes.
- A second gstApp exits non-zero before enable/rotate/prepare writes; after the
  first app exits, a new process acquires the lock and starts normally.
- A pre-existing READY with a stored rejected-request errno refreshes using its
  original generation, and a new full write followed by status-read failure is
  immediately cancelled.
- With `play_delay >= 60`, the first PAUSED transition consumes the lease before
  the driver's unused-lease timer can expire it.
- Single-left, single-right, dual-wide, partial failure, missing ABI, prepare
  interruption, and module reload are exercised.
- A 100-restart soak records the existing unmatched `s_power(1)` behavior and
  verifies that module unload/reload remains bounded and warning-free.
- Before canary rollout, every old gstApp process is stopped because an older
  binary does not participate in the new advisory flock.

## Explicit non-goals

- Fixing the parser's historical CSI FPS index assignment.
- Migrating `/root/shared_v` consumers to `/tmp/config`.
- Adding process supervision, PID registries, or lifecycle ownership for
  non-gstApp maintenance processes.
- Serializing or denying `init_cam.sh`, `kill_test.sh`, module reset, or hard
  reset commands. The gstApp-only owner flock is intentionally narrower.
- Treating READY/match as proof of GMSL, sensor, ISP, CSI, or frame health.
- Performing automatic module or hard reset from gstApp.
- Changing PR #38 RTSP, link-status, fragment, or sync-trace behavior.
