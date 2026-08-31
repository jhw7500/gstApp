#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

SDK_LOC=${SDK_LOC:-/shared/fsl-imx-xwayland/5.10-hardknott}
SDK_NAME=${SDK_NAME:-cortexa53-crypto-poky-linux}
QEMU=${QEMU:-qemu-aarch64}
SYSROOT="${SDK_LOC}/sysroots/${SDK_NAME}"

command -v "$QEMU" >/dev/null 2>&1 || {
    echo "missing QEMU runner: $QEMU" >&2
    exit 1
}

./make-for-imx8 bin/testParserConfig
"$QEMU" -L "$SYSROOT" bin/testParserConfig

