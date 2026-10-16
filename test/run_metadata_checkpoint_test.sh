#!/usr/bin/env bash
# Production JSON reader with deterministic stdio, steady-clock and RTOS bounds.
# Optional exact-store read counts: script --store /path/to/store
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
source="${RUNTIME_SOURCE:-$repo}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -DESP_PLATFORM=1
  -I"$source/src" -I"$source/sdk/app" -I"$source/sdk/driver" -I"$source/lib/ArduinoJson/src"
  -I"$repo/test/performance_loader_stubs"
  -Wl,--wrap=fopen,--wrap=fclose,--wrap=fread,--wrap=feof,--wrap=ferror,--wrap=malloc)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for trace in 0 1; do
  "${CXX:-c++}" "${flags[@]}" -DRISC_PERFORMANCE_TRACE="$trace" \
    "$source/src/bootstrap/Json.cpp" "$repo/test/metadata_checkpoint_test.cpp" -o "$build/test-$trace"
  "$build/test-$trace" "$@"
done
