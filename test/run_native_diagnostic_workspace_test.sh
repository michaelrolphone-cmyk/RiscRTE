#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -pedantic -g
 -DRISC_NATIVE_ESP_IDF=1 -DRISC_CONSOLE_USB_SERIAL_JTAG=0 -DRISC_ENABLE_USB_PHY=0
 -DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=1 -DCONFIG_SPIRAM_BOOT_INIT=1)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
includes=(-I"$repo/test/native_diagnostic_workspace_shim" -I"$repo/test/native_sleep_diagnostics_idf6_shim"
 -I"$repo/test/native_platform_shim" -I"$idf/components/esp_common/include" -I"$repo/src")
for capacity in 64 128; do
 for selection in '1 0' '0 1' '1 1'; do
  read -r diagnostics performance <<< "$selection"
  "${CXX:-c++}" "${flags[@]}" "${includes[@]}" -DRISC_SLEEP_DIAGNOSTICS="$diagnostics" -DRISC_PERFORMANCE_TRACE="$performance" \
   -DRISC_PERFORMANCE_TRACE_CAPACITY="$capacity" "$repo/test/native_diagnostic_workspace_test.cpp" -o "$build/workspace"
  "$build/workspace"
 done
done
