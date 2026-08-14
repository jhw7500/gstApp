# i.MX8 Build Wrapper ShellCheck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make-for-imx8`이 모든 인자를 그대로 전달하고 전체 저장소 ShellCheck CI를 통과하게 한다.

**Architecture:** fake SDK와 fake make를 주입하는 독립 shell test로 wrapper의 실제 argv/env 계약을 검증한다. 제품 wrapper는 quoting/export와 한정된 ShellCheck directive만 수정한다.

**Tech Stack:** Bash, ShellCheck, Makefile wrapper, Yocto SDK environment script

## Global Constraints

- 실제 SDK 경로와 기본 target toolchain 값은 변경하지 않는다.
- SDK environment script sourcing 동작을 유지한다.
- build는 반드시 수정된 `./make-for-imx8`을 통해 검증한다.

---

### Task 1: Wrapper argv/environment contract

**Files:**
- Create: `test/run-make-for-imx8-test.sh`
- Modify: `make-for-imx8`

- [ ] Write a fake SDK/fake make test that passes an argument containing spaces and a literal glob and asserts exact argv plus exported pkg-config variables.
- [ ] Run it before the wrapper change and confirm failure is caused by argument splitting/globbing.
- [ ] Quote SDK paths, export pkg-config variables, spell the empty directory as `PKG_CONFIG_DIR=''`, pass `"$@"`, and document only the dynamic source line for SC1090.
- [ ] Run the focused test and ShellCheck over every repository shell script.
- [ ] Run `./make-for-imx8` and verify the normal build remains successful.
- [ ] Commit and scoped-review only the wrapper/test diff.

### Task 2: Final aggregate gates

- [ ] Run `git diff --check`, source/config tests, all pure cross-built unit tests on target, and full i.MX8 build.
- [ ] Run short default and trace target smoke phases sequentially with exact service/binary/config restoration checks.
- [ ] Push the commits and request new review scoped to the new commit range before evaluating PR #38 again.

