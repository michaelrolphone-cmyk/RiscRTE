#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";idf="${IDF_PATH:?Set IDF_PATH to pinned headers or SDK}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O0 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-unused-function -Wno-unused-parameter -pedantic -no-pie -DRISC_NATIVE_ESP_IDF=1 -DRISC_NATIVE_SERIAL_FILES=1 -DRISC_SLEEP_DIAGNOSTICS=1 -DRISC_CONSOLE_USB_SERIAL_JTAG=0 -DRISC_ENABLE_USB_PHY=0)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie);fi
includes=(-I"$repo/test/native_diagnostic_workspace_shim" -I"$repo/test/native_sleep_diagnostics_idf6_shim" -I"$repo/test/native_platform_shim" -I"$idf/components/esp_common/include" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver")
for external in 0 1;do
 c++ "${flags[@]}" "${includes[@]}" -DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY="$external" -DCONFIG_SPIRAM_BOOT_INIT="$external" "$repo/test/stat_observation_native_test.cpp" "$repo/src/runtime/serial/Files.cpp" -o "$build/test"
 "$build/test"
done
