#!/usr/bin/env bash
set -euo pipefail
task_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_build=$(mktemp -d "${TMPDIR:-/tmp}/frame-binding-throughput.XXXXXXXX")
trap 'rm -rf -- "$task_build"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread \
    -I "$task_root" "$task_root/tests/binding_throughput_fixture.cpp" \
    -o "$task_build/binding_throughput_fixture"
"$task_build/binding_throughput_fixture" "$@"
