#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DRISC_NATIVE_ESP_IDF=1 -DRISC_CONSOLE_USB_SERIAL_JTAG=1 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
includes=(-I"$repo/test/native_sleep_diagnostics_idf6_shim" -I"$repo/test/native_platform_shim"
 -I"$idf/components/esp_common/include" -I"$idf/components/esp_driver_usb_serial_jtag/include"
 -I"$idf/components/soc/esp32s3/include" -I"$idf/components/soc/include"
 -I"$repo/src" -I"$repo/sdk/driver")
for stage in 0 1; do
 "${CXX:-c++}" "${flags[@]}" "${includes[@]}" -DRISC_ENABLE_USB_PHY=0 -DRISC_STAGE_LOGS="$stage" -DRISC_SLEEP_DIAGNOSTICS=0 \
  "$repo/test/native_console_records_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" \
  "$repo/src/ports/esp32s3/NativePlatform.cpp" -o "$build/native-console-records"
 "$build/native-console-records"
done
