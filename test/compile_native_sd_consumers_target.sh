#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_sd_consumers_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide an output directory}"
compiler="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
test -x "$compiler"; mkdir -p "$out"
"$compiler" --version
"$compiler" -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 -c "$root/test/native_sd_consumers_target_probe.cpp" -o "$out/native-sd-consumers.o"
"$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm" -S "$out/native-sd-consumers.o" | grep 'native_sd_.*_size'
sha256sum "$out/native-sd-consumers.o"
