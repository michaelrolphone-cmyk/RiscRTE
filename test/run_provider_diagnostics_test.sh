#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for enabled in 1 0;do
  "${CXX:-c++}" "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
    -I"$repo/src" -DRISC_SLEEP_DIAGNOSTICS="$enabled" \
    -DRISC_STAGE_LOGS=0 -DRISC_PERFORMANCE_TRACE=0 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=0 \
    -DRISC_ENABLE_USB_PHY=0 -DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=0 \
    "$repo/src/ports/esp32s3/ProviderDiagnostics.cpp" \
    "$repo/test/provider_diagnostics_test.cpp" -o "$build/test"
  "$build/test"
  echo "provider diagnostics adapter=$enabled PASS"
done
