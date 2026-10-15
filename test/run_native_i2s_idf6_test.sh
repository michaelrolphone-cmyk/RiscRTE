#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
if [[ ! -f "$idf/components/esp_driver_i2s/include/driver/i2s_std.h" ]]; then
  echo "Set IDF_PATH to the selected ESP-IDF6 source checkout" >&2
  exit 1
fi
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
# I2S driver headers, I2S HAL types, error constants, version and S3 capability
# declarations are the real SDK. Only GPIO/RTC, task and time calls are mocked.
"${CXX:-c++}" "${flags[@]}" \
  -I"$repo/test/native_i2s_idf6_shim" \
  -I"$idf/components/esp_common/include" \
  -I"$idf/components/esp_driver_i2s/include" \
  -I"$idf/components/esp_hal_i2s/include" \
  -I"$idf/components/soc/esp32s3/include" \
  -I"$idf/components/soc/include" \
  -I"$repo/test/native_i2s_shim" -I"$repo/test/native_sleep_shim" \
  -I"$repo/sdk/hardware" -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/test/native_i2s_idf6_test.cpp" -o "$build/native-i2s-idf6"
"$build/native-i2s-idf6"
