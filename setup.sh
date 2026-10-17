#!/usr/bin/env bash
# User-scope only. No sudo, Steam restart, automatic restart or hidden background task.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec /usr/bin/python3 -B "$root/tools/native_passthrough_setup.py" "$@"
