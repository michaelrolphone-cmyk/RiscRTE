#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_import_initialization_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide an output directory}"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
compiler="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
test -x "$compiler"; mkdir -p "$out"
"$compiler" --version
"$compiler" -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -DESP_PLATFORM \
 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 -I"$sdk/include/nvs_flash/include" -I"$sdk/include/esp_common/include" \
 -I"$sdk/include/esp_hw_support/include" -I"$sdk/include/esp_rom/include" -I"$sdk/qio_qspi/include" \
 -c "$root/test/native_import_initialization_target_probe.cpp" -o "$out/native-import-initialization.o"
"$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm" -S "$out/native-import-initialization.o" | grep native_import_.*_size
sha256sum "$out/native-import-initialization.o"
