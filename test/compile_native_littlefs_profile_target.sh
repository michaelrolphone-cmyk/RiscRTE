#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_littlefs_profile_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide an output directory}"
compiler="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
test -x "$compiler";mkdir -p "$out"
version="$("$compiler" --version | head -1)"
[[ "$version" == *'crosstool-NG esp-2021r2-patch5'*'8.4.0' ]]
printf '%s\n' "$version"
"$compiler" -std=c++17 -Wall -Wextra -Werror -Os -mlongcalls -fno-exceptions -fno-rtti -fstack-usage -DESP_PLATFORM \
 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 -c "$root/test/native_littlefs_profile_target_probe.cpp" -o "$out/native-littlefs-profile.o"
"${compiler%g++}nm" -u "$out/native-littlefs-profile.o"
"${compiler%g++}objdump" -s -j .rodata "$out/native-littlefs-profile.o"
cat "$out/native-littlefs-profile.su"
sha256sum "$out/native-littlefs-profile.o"
echo 'PASS: separately supplied profile compiles; no production mount adapter or selection'
