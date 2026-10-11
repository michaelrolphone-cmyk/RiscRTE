#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_export_marker_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide an output directory}"
compiler="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
test -x "$compiler"; mkdir -p "$out"
"$compiler" --version
"$compiler" -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
 -I"$root" -c "$root/test/native_export_marker_target_probe.cpp" -o "$out/native-export-marker-target-probe.o"
sha256sum "$out/native-export-marker-target-probe.o"
