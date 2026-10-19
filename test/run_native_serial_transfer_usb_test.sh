#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:?Set IDF_PATH to pinned SDK headers}"
build="${OUTPUT_DIR:?Set OUTPUT_DIR to fresh evidence directory}";mkdir -p "$build/files"
flags=(-std=c++17 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-return-type
 -DRISC_NATIVE_ESP_IDF=1 -DRISC_CONSOLE_USB_SERIAL_JTAG=1 -DRISC_NATIVE_SERIAL_FILES=1 -DRISC_NATIVE_SERIAL_TRANSFER_MODE=1
 -DRISC_ENABLE_USB_PHY=1 -DRISC_STAGE_LOGS=1 -DRISC_SLEEP_DIAGNOSTICS=1
 -DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=1 -DCONFIG_SPIRAM_BOOT_INIT=1)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
includes=(-I"$repo/test/native_diagnostic_workspace_shim" -I"$repo/test/native_sleep_diagnostics_idf6_shim" -I"$repo/test/native_platform_shim"
 -I"$idf/components/esp_common/include" -I"$idf/components/esp_driver_usb_serial_jtag/include"
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
set -x
"${CXX:-c++}" "${flags[@]}" "${includes[@]}" "$repo/test/native_serial_transfer_usb_test.cpp" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/NativePlatform.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" \
 "$repo/src/ports/esp32s3/NativeSerialFiles.cpp" "$repo/src/runtime/serial/Files.cpp" -ldl -o "$build/test"
"$build/test" "$build/files"
