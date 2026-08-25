#!/bin/bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT

SDK_LOC="$TEST_DIR/sdk with spaces"
SDK_NAME="test toolchain"
SDK_ENV="$SDK_LOC/environment-setup-$SDK_NAME"
FAKE_BIN="$TEST_DIR/fake-bin"
WORK_DIR="$TEST_DIR/work"
CAPTURE_FILE="$TEST_DIR/make-capture"
COMPILEDB_CAPTURE_FILE="$TEST_DIR/compiledb-capture"
EXPECTED_FILE="$TEST_DIR/expected-capture"

mkdir -p "$SDK_LOC/sysroots/$SDK_NAME/usr/lib/pkgconfig" "$FAKE_BIN" "$WORK_DIR"

cat > "$SDK_ENV" <<'SDK_EOF'
export SDK_ENV_SOURCED='configured-sdk-environment'
export PATH="$FAKE_BIN:$PATH"
SDK_EOF

cat > "$FAKE_BIN/make" <<'MAKE_EOF'
#!/bin/bash
{
    printf 'argc=%s\n' "$#"
    for arg in "$@"
    do
        printf 'arg=%s\n' "$arg"
    done
    printf 'PKG_CONFIG_SYSROOT_DIR=%s\n' "${PKG_CONFIG_SYSROOT_DIR-__UNSET__}"
    printf 'PKG_CONFIG_PATH=%s\n' "${PKG_CONFIG_PATH-__UNSET__}"
    printf 'PKG_CONFIG_DIR=%s\n' "${PKG_CONFIG_DIR-__UNSET__}"
    printf 'SDK_ENV_SOURCED=%s\n' "${SDK_ENV_SOURCED-__UNSET__}"
} > "$CAPTURE_FILE"
MAKE_EOF
chmod +x "$FAKE_BIN/make"

# make-for-imx8 는 빌드 후 compiledb 로 compile_commands.json 을 갱신한다.
# 실제 compiledb 는 make 를 한 번 더 부르므로, 여기서는 argv 만 기록하는 가짜를
# 둔다. 그래야 위의 make 캡처가 "빌드 1회" 계약을 그대로 검증한다.
cat > "$FAKE_BIN/compiledb" <<'COMPILEDB_EOF'
#!/bin/bash
{
    printf 'argc=%s\n' "$#"
    for arg in "$@"
    do
        printf 'arg=%s\n' "$arg"
    done
} > "$COMPILEDB_CAPTURE_FILE"
COMPILEDB_EOF
chmod +x "$FAKE_BIN/compiledb"

touch "$WORK_DIR/literal-expanded.target"

(
    cd "$WORK_DIR"
    export SDK_LOC SDK_NAME FAKE_BIN CAPTURE_FILE COMPILEDB_CAPTURE_FILE
    unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_DIR SDK_ENV_SOURCED
    "$ROOT_DIR/make-for-imx8" 'target with spaces' 'literal-*.target'
)

{
    printf 'argc=2\n'
    printf 'arg=%s\n' 'target with spaces'
    printf 'arg=%s\n' 'literal-*.target'
    printf 'PKG_CONFIG_SYSROOT_DIR=%s\n' "$SDK_LOC/sysroots/$SDK_NAME"
    printf 'PKG_CONFIG_PATH=%s\n' "$SDK_LOC/sysroots/$SDK_NAME/usr/lib/pkgconfig"
    printf 'PKG_CONFIG_DIR=\n'
    printf 'SDK_ENV_SOURCED=configured-sdk-environment\n'
} > "$EXPECTED_FILE"

if ! cmp -s "$EXPECTED_FILE" "$CAPTURE_FILE"
then
    printf 'FAIL: make-for-imx8 did not preserve the exact argv/environment contract\n' >&2
    printf '%s\n' '--- expected' >&2
    cat "$EXPECTED_FILE" >&2
    printf '%s\n' '--- actual' >&2
    cat "$CAPTURE_FILE" >&2
    exit 1
fi

COMPILEDB_EXPECTED_FILE="$TEST_DIR/compiledb-expected"
{
    printf 'argc=6\n'
    printf 'arg=-n\n'
    printf 'arg=-o\n'
    printf 'arg=.compile_commands.json.tmp\n'
    printf 'arg=make\n'
    printf 'arg=%s\n' 'target with spaces'
    printf 'arg=%s\n' 'literal-*.target'
} > "$COMPILEDB_EXPECTED_FILE"

if ! cmp -s "$COMPILEDB_EXPECTED_FILE" "$COMPILEDB_CAPTURE_FILE"
then
    printf 'FAIL: compile_commands.json 갱신이 빌드 인자를 그대로 넘기지 않았다\n' >&2
    printf '%s\n' '--- expected' >&2
    cat "$COMPILEDB_EXPECTED_FILE" >&2
    printf '%s\n' '--- actual' >&2
    cat "$COMPILEDB_CAPTURE_FILE" 2>/dev/null >&2 || printf '(캡처 없음)\n' >&2
    exit 1
fi

ZERO_CAPTURE_FILE="$TEST_DIR/zero-argv-capture"
ZERO_EXPECTED_FILE="$TEST_DIR/zero-argv-expected"

(
    cd "$WORK_DIR"
    CAPTURE_FILE="$ZERO_CAPTURE_FILE"
    export SDK_LOC SDK_NAME FAKE_BIN CAPTURE_FILE COMPILEDB_CAPTURE_FILE
    unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_DIR SDK_ENV_SOURCED
    "$ROOT_DIR/make-for-imx8"
)

{
    printf 'argc=0\n'
    printf 'PKG_CONFIG_SYSROOT_DIR=%s\n' "$SDK_LOC/sysroots/$SDK_NAME"
    printf 'PKG_CONFIG_PATH=%s\n' "$SDK_LOC/sysroots/$SDK_NAME/usr/lib/pkgconfig"
    printf 'PKG_CONFIG_DIR=\n'
    printf 'SDK_ENV_SOURCED=configured-sdk-environment\n'
} > "$ZERO_EXPECTED_FILE"

if ! cmp -s "$ZERO_EXPECTED_FILE" "$ZERO_CAPTURE_FILE"
then
    printf 'FAIL: zero-argument invocation did not preserve argc=0 and the environment contract\n' >&2
    printf '%s\n' '--- expected' >&2
    cat "$ZERO_EXPECTED_FILE" >&2
    printf '%s\n' '--- actual' >&2
    cat "$ZERO_CAPTURE_FILE" >&2
    exit 1
fi

FAIL_SDK_LOC="$TEST_DIR/failing sdk"
FAIL_SDK_ENV="$FAIL_SDK_LOC/environment-setup-$SDK_NAME"
FAIL_CAPTURE_FILE="$TEST_DIR/failing-source-make-capture"
mkdir -p "$FAIL_SDK_LOC"

cat > "$FAIL_SDK_ENV" <<'FAIL_SDK_EOF'
export SDK_ENV_SOURCED='failing-sdk-environment'
return 37
FAIL_SDK_EOF

set +e
(
    cd "$WORK_DIR"
    SDK_LOC="$FAIL_SDK_LOC"
    CAPTURE_FILE="$FAIL_CAPTURE_FILE"
    export SDK_LOC SDK_NAME FAKE_BIN CAPTURE_FILE COMPILEDB_CAPTURE_FILE
    export PATH="$FAKE_BIN:$PATH"
    unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_DIR SDK_ENV_SOURCED
    "$ROOT_DIR/make-for-imx8"
)
source_status=$?
set -e

if [ "$source_status" -ne 37 ]
then
    printf 'FAIL: failing SDK environment returned 37 but wrapper returned %s\n' "$source_status" >&2
    exit 1
fi

if [ -e "$FAIL_CAPTURE_FILE" ]
then
    printf 'FAIL: make was invoked after the SDK environment returned 37\n' >&2
    exit 1
fi

printf 'PASS: make-for-imx8 preserves argv, environment, and SDK source failures\n'
