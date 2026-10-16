#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
cxx="${1:?Xtensa GCC 15 C++ compiler required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
"$cxx" --version | head -1
"$cxx" -std=gnu++17 -Wall -Wextra -Werror -Os -fno-exceptions -fno-rtti -fstack-usage -DESP_PLATFORM -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -c "$repo/test/runtime_files_target_probe.cpp" -o "$build/source.o"
cat "$build/source.su"
printf 'PASS: Xtensa generic internal source/provider compile, fixed-state bounds and native no-replace unavailability\n'
