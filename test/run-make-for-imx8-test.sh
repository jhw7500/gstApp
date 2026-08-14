#!/bin/bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT

SDK_LOC="$TEST_DIR/sdk"
SDK_NAME="test-toolchain"
SDK_ENV="$SDK_LOC/environment-setup-$SDK_NAME"
FAKE_BIN="$TEST_DIR/fake-bin"
WORK_DIR="$TEST_DIR/work"
CAPTURE_FILE="$TEST_DIR/make-capture"
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

touch "$WORK_DIR/literal-expanded.target"

(
    cd "$WORK_DIR"
    export SDK_LOC SDK_NAME FAKE_BIN CAPTURE_FILE
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

printf 'PASS: make-for-imx8 preserves argv and exported pkg-config environment\n'
