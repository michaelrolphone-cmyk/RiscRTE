#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -DRISC_NATIVE_ESP_IDF=1 -DRISC_CONSOLE_USB_SERIAL_JTAG=1 -DRISC_NATIVE_SERIAL_FILES=1)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g);fi
includes=(-I"$repo/test/native_sleep_diagnostics_idf6_shim" -I"$repo/test/native_platform_shim"
 -I"$idf/components/esp_common/include" -I"$idf/components/esp_driver_usb_serial_jtag/include"
 -I"$idf/components/soc/esp32s3/include" -I"$idf/components/soc/include"
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware"
 -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp"
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp"
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp"
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/runtime/serial/Files.cpp"
 "$repo/src/ports/esp32s3/NativeSerialFiles.cpp" "$repo/src/ports/esp32s3/NativePlatform.cpp"
 "$repo/src/ports/esp32s3/SleepDiagnostics.cpp")
for config in '0 0 0' '1 1 1';do
 read -r phy stage diagnostics <<< "$config"
 "${CXX:-c++}" "${flags[@]}" "${includes[@]}" -DRISC_ENABLE_USB_PHY="$phy" -DRISC_STAGE_LOGS="$stage" -DRISC_SLEEP_DIAGNOSTICS="$diagnostics" \
   "${sources[@]}" "$repo/test/native_serial_files_transport_test.cpp" -ldl -o "$build/native"
 mkdir "$build/files-$phy"
 "$build/native" "$build/files-$phy" absent
 "$build/native" "$build/files-$phy"
done
# An unselected adapter remains an empty object and creates no endpoint state.
"${CXX:-c++}" -std=c++17 -I"$repo/src" -c "$repo/src/ports/esp32s3/NativeSerialFiles.cpp" -o "$build/default-off.o"
[[ -z "$(nm "$build/default-off.o")" ]]
echo 'Native serial default-off empty object PASS'
