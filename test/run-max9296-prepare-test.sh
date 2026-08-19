#!/bin/bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

g++ -std=c++11 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -pthread \
    -I"$ROOT_DIR" "$ROOT_DIR/max9296Prepare.cpp" \
    "$ROOT_DIR/test/test_max9296Prepare.cpp" -o "$BUILD_DIR/test_max9296Prepare"
"$BUILD_DIR/test_max9296Prepare"
