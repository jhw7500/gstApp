#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

python3 - "$BUILD_DIR/test_safe_write_file.cpp" <<'PY'
from pathlib import Path
import sys


def function_text(source, signature):
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f'missing production function: {signature}')
    opening = source.find('{', start)
    depth = 1
    pos = opening + 1
    while pos < len(source) and depth:
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
        pos += 1
    if depth:
        raise SystemExit(f'unterminated production function: {signature}')
    return source[start:pos]


production = Path('util.cpp').read_text(encoding='utf-8')
safe_write = function_text(production, 'int safe_write_file(')
test_source = r'''
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_ERR 0
#define _FILE_ "test"
static void log_clobber(int, const char *, ...)
{
    errno = EIO;
}
#define __LOG(...) log_clobber(__VA_ARGS__)

''' + safe_write + r'''

int main(void)
{
    errno = 0;
    const int full_result = safe_write_file("/dev/full", "1");
    const int full_errno = errno;
    if (full_result >= 0) {
        fprintf(stderr,
                "safe_write_file(/dev/full) unexpectedly reported success\n");
        return 1;
    }
    if (full_errno != ENOSPC) {
        fprintf(stderr,
                "safe_write_file(/dev/full) lost ENOSPC: got %d\n",
                full_errno);
        return 2;
    }

    char missing_parent[] = "/tmp/safe-write-missing-XXXXXX";
    if (mkdtemp(missing_parent) == NULL || rmdir(missing_parent) != 0)
        return 3;
    char missing_path[128] = {};
    if (snprintf(missing_path, sizeof(missing_path), "%s/file",
                 missing_parent) >= (int)sizeof(missing_path))
        return 4;
    errno = 0;
    const int missing_result = safe_write_file(missing_path, "1");
    const int missing_errno = errno;
    if (missing_result >= 0 || missing_errno != ENOENT) {
        fprintf(stderr,
                "safe_write_file(missing parent) lost ENOENT: result=%d errno=%d\n",
                missing_result, missing_errno);
        return 5;
    }

    char path[] = "/tmp/safe-write-file-XXXXXX";
    const int seed = mkstemp(path);
    if (seed < 0 || close(seed) != 0)
        return 6;
    if (safe_write_file(path, "camera") != 0)
        return 7;

    FILE *fp = fopen(path, "r");
    char content[16] = {};
    const bool read_ok = fp && fgets(content, sizeof(content), fp);
    const bool close_ok = fp && fclose(fp) == 0;
    unlink(path);
    if (!read_ok || !close_ok || strcmp(content, "camera") != 0)
        return 8;

    puts("safe write file test: PASSED");
    return 0;
}
'''
Path(sys.argv[1]).write_text(test_source, encoding='utf-8')
PY

${CXX:-g++} -std=c++11 -Wall -Wextra -Werror \
  -o "$BUILD_DIR/test_safe_write_file" "$BUILD_DIR/test_safe_write_file.cpp"
"$BUILD_DIR/test_safe_write_file"
