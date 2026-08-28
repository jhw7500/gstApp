#!/bin/bash
set -Eeuo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
test_bin=$(mktemp /tmp/test-max9296-controls.XXXXXX)

cleanup()
{
    rm -f "$test_bin"
}
trap cleanup EXIT

g++ -std=c++11 -Wall -Wextra -Werror \
    "$repo_dir/test/test_max9296Controls.cpp" \
    "$repo_dir/max9296Controls.cpp" \
    -o "$test_bin"

"$test_bin"

for contract in \
    'gboolean crop_enable[MAX_VIDEO_SRC];' \
    'arg.crop_enable[csi] = FALSE;' \
    'json_object_object_get(sobj, "crop_enable")' \
    'json_type_boolean' \
    'max9296_crop_enable_normalize'; do
    if ! rg -F -- "$contract" "$repo_dir/util.h" "$repo_dir/parser.cpp" >/dev/null; then
        echo "FAIL: crop parser contract missing: $contract" >&2
        exit 1
    fi
done

if ! rg -F -- 'gboolean crop_en[2];' "$repo_dir/util.h" >/dev/null; then
    echo "FAIL: GStreamer crop_en state was removed or aliased" >&2
    exit 1
fi
