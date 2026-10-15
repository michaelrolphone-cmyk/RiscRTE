#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
for header in esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h esp_driver_uart/include/driver/uart.h; do
  if [[ ! -f "$idf/components/$header" ]]; then
    echo "Set IDF_PATH to the selected ESP-IDF6 source checkout" >&2
    exit 1
  fi
done
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -DRISC_NATIVE_ESP_IDF=1)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
# Both driver declarations, UART HAL types, errors and S3 capabilities are from
# the selected real SDK. Only FreeRTOS, timer, heap and logging are mocked.
includes=(-I"$repo/test/native_platform_shim"
  -I"$idf/components/esp_common/include"
  -I"$idf/components/esp_driver_usb_serial_jtag/include"
  -I"$idf/components/esp_driver_uart/include"
  -I"$idf/components/esp_hal_uart/include"
  -I"$idf/components/soc/esp32s3/include"
  -I"$idf/components/soc/include"
  -I"$repo/src")
for usb in 1 0; do
  "${CXX:-c++}" "${flags[@]}" "${includes[@]}" -DRISC_CONSOLE_USB_SERIAL_JTAG="$usb" \
    "$repo/test/native_platform_test.cpp" "$repo/src/ports/esp32s3/NativePlatform.cpp" \
    -o "$build/native-platform-$usb"
  "$build/native-platform-$usb"
done
