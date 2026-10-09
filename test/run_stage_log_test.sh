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
  for recorder in 0 1; do
    "${CXX:-c++}" "${flags[@]}" -DRISC_STAGE_LOGS=1 -DRISC_SLEEP_DIAGNOSTICS=$diagnostics -DRISC_PERFORMANCE_TRACE=$recorder \
      -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
      -I"$repo/test/stage_shim" -I"$repo/test/diagnostic_shim" -I"$repo/src" \
      "$repo/test/stage_log_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/test"
    "$build/test"
  done
done
# Disabled statement arguments, strings and formatting/clock symbols disappear,
# including unoptimized production code.
cat > "$build/disabled.cpp" <<'CPP'
#include "diagnostics/StageLog.h"
#include <cassert>
int main(){int calls=0;RISC_STAGE_LOG("must-not-exist-%d",++calls);assert(calls==0);}
CPP
"${CXX:-c++}" "${flags[@]}" -O0 -DESP_PLATFORM=1 -DRISC_STAGE_LOGS=0 -I"$repo/src" \
  "$build/disabled.cpp" -o "$build/disabled"
"$build/disabled"
if nm -C "$build/disabled" | grep -E 'RiscDiagnostics::|esp_timer|snprintf'; then
  echo 'disabled stage log retains observer code' >&2;exit 1
fi
if strings "$build/disabled" | grep 'must-not-exist'; then
  echo 'disabled stage log retains format string' >&2;exit 1
fi
echo 'Disabled plain stage logs: no formatting, clock, storage or argument evaluation PASS'
