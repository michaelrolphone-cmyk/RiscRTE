#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O1 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-parameter -Wno-misleading-indentation)
if [[ "${SANITIZE:-0}" == 1 ]];then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g)
fi
for stage in 0 1;do
  for diagnostics in 0 1;do
    "${CXX:-c++}" "${flags[@]}" \
      -DRISC_ENABLE_USB_PHY=1 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1 \
      -DRISC_STAGE_LOGS="$stage" -DRISC_SLEEP_DIAGNOSTICS="$diagnostics" -DRISC_PERFORMANCE_TRACE=0 \
      -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
      -I"$repo/test/hwcdc_pinned/shim" -I"$repo/test/hwcdc_pinned/vendor" \
      -I"$repo/src" -I"$repo/sdk/driver" \
      "$repo/test/provider_diagnostics_sink_test.cpp" \
      "$repo/src/ports/esp32s3/ProviderDiagnostics.cpp" \
      "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/test"
    "$build/test"
    echo "provider diagnostics sink stage=$stage recorder=$diagnostics PASS"
  done
done
