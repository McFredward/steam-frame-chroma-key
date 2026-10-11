#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_tmp="$(mktemp -d "${TMPDIR:-/tmp}/mr-mask-test.XXXXXX")"
trap 'rm -rf -- "$task_tmp"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$root/mask/mask_protocol.cpp" "$root/tests/test_mask_protocol.cpp" \
    -o "$task_tmp/test-mask-protocol"
"$task_tmp/test-mask-protocol"
