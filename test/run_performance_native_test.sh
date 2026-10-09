#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for diagnostics in 0 1; do
"${CXX:-c++}" "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=$diagnostics -DRISC_PERFORMANCE_TRACE=1 \
  -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
  -I"$repo/test/performance_shim" -I"$repo/test/diagnostic_shim" -I"$repo/src" \
  "$repo/test/performance_native_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/test"
"$build/test"
done
