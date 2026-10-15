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
for config in '1 0 0' '1 0 1' '1 1 0' '1 1 1' '0 1 1'; do
 read -r phy stage diagnostics <<< "$config"
 "${CXX:-c++}" "${flags[@]}" "${includes[@]}" -DRISC_ENABLE_USB_PHY="$phy" -DRISC_STAGE_LOGS="$stage" -DRISC_SLEEP_DIAGNOSTICS="$diagnostics" \
  "$repo/test/native_sleep_diagnostics_idf6_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" \
  "$repo/src/ports/esp32s3/NativePlatform.cpp" -o "$build/native-usb-diagnostics"
 "$build/native-usb-diagnostics"
done
