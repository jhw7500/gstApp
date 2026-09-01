# Edgeconf Array Startup-Fatal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make explicitly malformed edgeconf stream arrays abort gstApp startup while preserving optional-missing and existing recoverable fallback behavior.

**Architecture:** Keep array decoding pure in `cfgjson`, while using
`json_object_object_get_ex()` to distinguish an absent optional key from an
explicit JSON null. Add a dedicated structural-error policy at the parser call
site, exercise the real `ParserClass::json_parser()` with temporary JSON
fixtures, and rely on the existing `main.cpp` negative-return gate to stop
before hardware initialization.

**Tech Stack:** C++11-style project code, GLib, json-c, Make, Yocto i.MX8 cross compiler, QEMU user-mode execution, Bash/Python source-contract test.

**Spec:** `docs/superpowers/specs/2026-08-31-edgeconf-array-fatal-design.md`

## Global Constraints

- `bps`, `gop`, `profile`, `quant`, `qp_min`, and `qp_max` are exact two-element `[record, rtsp]` arrays.
- `CFG_ARR_NOT_ARRAY`, `CFG_ARR_BAD_LEN`, and `CFG_ARR_BAD_ELEM` are startup-fatal; `CFG_ARR_MISSING` remains nonfatal.
- Existing boolean, crop, encoder-name, and numeric-range fallback policies remain recoverable.
- Diagnostics include channel, key, expected length, and error kind.
- No new dependency or hardware write is introduced.

---

### Task 1: Real parser regression test

**Files:**
- Create: `test/test_parser_config.cpp`
- Modify: `Makefile`
- Create: `test/run-parser-config-test.sh`

**Interfaces:**
- Consumes: `ParserClass::init_arg(gchar *)`, `ParserClass::json_parser(const gchar *, const gchar *)`
- Produces: `bin/testParserConfig` and a QEMU-backed executable regression runner

- [x] **Step 1: Write the failing integration test**

Add temporary edgeconf fixtures that call the production parser and assert:

```cpp
CHECK(parse_edgeconf(malformed_bps) < 0);
CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
CHECK(parse_edgeconf(missing_arrays) == 0);
CHECK(parse_edgeconf(recoverable_boolean_error) == 0);
```

The malformed fixture also contains a valid later-channel value and a second
malformed array. Assert that the later value is applied and captured diagnostics
contain both error contexts plus the final count, proving collection does not
stop at the first error.

- [x] **Step 2: Cross-build and run to verify RED**

Run:

```bash
./make-for-imx8 bin/testParserConfig
bash test/run-parser-config-test.sh
```

Expected: the runner exits nonzero because current `json_parser()` returns `0`
for the malformed fixture.

- [x] **Step 3: Keep the test target production-faithful**

Compile `parser.cpp`, `cfgjson.cpp`, and `max9296Controls.cpp` with function/data
sections and linker garbage collection. Test doubles provide only the logging
sink and `search_file()` filesystem-selection boundary; assertions exercise the
real parser and parsed `CmdArg` state.

### Task 2: Dedicated startup-fatal array policy

**Files:**
- Modify: `cfgjson.cpp`
- Modify: `parser.cpp`
- Test: `test/test_cfgjson.cpp`
- Test: `test/test_parser_config.cpp`

**Interfaces:**
- Consumes: `CfgArrStatus cfg_get_int_array(...)`
- Produces: negative `ParserClass::json_parser()` result when any explicit array is malformed

- [x] **Step 1: Distinguish absent keys from explicit null**

Use `json_object_object_get_ex()` so an absent optional array remains
`CFG_ARR_MISSING`, while an explicitly present null becomes
`CFG_ARR_NOT_ARRAY`. Lock both outcomes with `cfgjson` and real parser tests.

- [x] **Step 2: Add the minimal parse-local fatal count**

Add a parser-local counter separate from `g_cfg_errors`. Reset it at the start of
each `json_parser()` call and increment it only for:

```cpp
CFG_ARR_NOT_ARRAY
CFG_ARR_BAD_LEN
CFG_ARR_BAD_ELEM
```

- [x] **Step 3: Add actionable context without early return**

Pass the channel index into the array wrapper and emit one diagnostic per error
with `chN`, the key, `expected=<derived array length>`, and a stable error kind.
Do not return from the channel loop.

- [x] **Step 4: Return failure after complete traversal**

After all channels have been parsed, emit a critical summary and return `-1`
when the dedicated counter is nonzero. Continue returning `0` for missing arrays
and existing recoverable errors.

- [x] **Step 5: Run focused tests to verify GREEN**

Run:

```bash
bash test/run-parser-config-test.sh
qemu-aarch64 -L /shared/fsl-imx-xwayland/5.10-hardknott/sysroots/cortexa53-crypto-poky-linux bin/testCfgjson
```

Expected: all parser scenarios and all existing cfgjson checks pass.

### Task 3: Startup contract and operator documentation

**Files:**
- Modify: `test/run-max9296-prepare-source-check.sh`
- Create: `docs/EDGE_CONFIG_ARRAYS.md`

**Interfaces:**
- Consumes: the negative `json_parser()` return contract
- Produces: an enforced pre-hardware exit gate and documented array schema

- [x] **Step 1: Strengthen the startup contract test**

Require `main()` to return failure when `parser->json_parser(...) < 0`, and keep
the existing ordering assertion that the parser gate precedes owner lock, sysfs,
video initialization, MAX9296 preparation, and GStreamer state changes.

- [x] **Step 2: Document the schema and failure policy**

Document all six keys as exact `[record, rtsp]` pairs, state that omission keeps
defaults, and list wrong type, wrong length, and non-integer elements as fatal.

- [x] **Step 3: Run complete verification**

Run:

```bash
bash test/run-parser-config-test.sh
bash test/run-max9296-prepare-source-check.sh
./make-for-imx8 bin/testCfgjson
./make-for-imx8 bin/gstApp
bash -n test/run-parser-config-test.sh test/run-cfgjson-test.sh test/run-max9296-prepare-source-check.sh
```

Expected: all commands exit `0`; the application and both focused tests are
aarch64 binaries.

- [x] **Step 4: Commit the reviewed implementation**

Stage only the issue #69 files and commit with:

```bash
git commit -m "fix(parser): reject malformed edgeconf arrays at startup"
```
