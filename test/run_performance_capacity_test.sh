#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for capacity in 64 128; do
 "${CXX:-c++}" "${flags[@]}" -DRISC_PERFORMANCE_TRACE_CAPACITY="$capacity" -I"$repo/src" \
  "$repo/test/performance_capacity_test.cpp" -o "$build/capacity-$capacity"
 "$build/capacity-$capacity"
 # Existing recorder behavior, not only a synthetic size calculation.
 "${CXX:-c++}" "${flags[@]}" -DRISC_PERFORMANCE_TRACE_CAPACITY="$capacity" \
  -I"$repo/src" -I"$repo/sdk/app" "$repo/test/performance_recorder_test.cpp" -o "$build/recorder-$capacity"
 "$build/recorder-$capacity"
 for diagnostics in 0 1; do
  "${CXX:-c++}" "${flags[@]}" -DRISC_PERFORMANCE_TRACE_CAPACITY="$capacity" \
   -DRISC_SLEEP_DIAGNOSTICS="$diagnostics" -DRISC_PERFORMANCE_TRACE=1 \
   -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
   -I"$repo/test/performance_shim" -I"$repo/test/diagnostic_shim" -I"$repo/src" \
   "$repo/test/performance_native_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/native-$capacity-$diagnostics"
  "$build/native-$capacity-$diagnostics"
 done
done
for invalid in 0 15 129 4096; do
 if printf '#include "diagnostics/Performance.h"\n' | "${CXX:-c++}" -std=c++17 -DRISC_PERFORMANCE_TRACE_CAPACITY="$invalid" -I"$repo/src" -x c++ -fsyntax-only - 2>"$build/error"; then
  echo "invalid capacity $invalid was accepted" >&2;exit 1
 fi
 grep -q 'RISC_PERFORMANCE_TRACE_CAPACITY must be between 16 and 128 records' "$build/error"
done
echo 'Performance ring capacity: defaults unchanged, explicit64history bounded, invalid selections fail closed PASS'
