#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?PlatformIO packages directory required}"
out="${2:?Evidence directory required}"
mount="${MOUNT_ROOT:-$root}"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
cxx="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
nm="${cxx%g++}nm"
mkdir -p "$out"
includes=(-I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src" -I"$mount/src/ports/esp32s3" -I"$sdk/qio_opi/include" -I"$sdk/include/freertos/include/esp_additions" -I"$sdk/include/freertos/include/esp_additions/freertos" -I"$sdk/include/freertos/port/xtensa/include/freertos")
while IFS= read -r directory;do includes+=(-I"$directory");done < <(find "$sdk/include" -type d \( -name include -o -name platform_include -o -path '*/include/esp32s3' -o -path '*/include/xtensa' -o -path '*/port/xtensa/include' \) | sort)
flags=(-std=gnu++17 -Wall -Wextra -Werror -Wno-misleading-indentation -Os -mlongcalls -fno-exceptions -fno-rtti -fstack-usage -DESP_PLATFORM -DRISC_PAIRED_APP_DATA=1 -DRISC_PAIRED_BANKS=1)
"$cxx" --version
"$cxx" "${flags[@]}" "${includes[@]}" -DRISC_NATIVE_APP_DATA_MOUNT=1 -DRISC_NATIVE_FILE_SOURCE=1 -c "$root/src/ports/esp32s3/NativeFileSource.cpp" -o "$out/native-on.o"
"$cxx" "${flags[@]}" "${includes[@]}" -c "$root/src/ports/esp32s3/NativeFileSource.cpp" -o "$out/native-off.o"
"$cxx" "${flags[@]}" "${includes[@]}" -c "$root/test/native_file_source_target_probe.cpp" -o "$out/runtime-layout.o"
[[ -z "$("$nm" "$out/native-off.o")" ]]
sha256sum "$out"/*.o
cat "$out"/*.su
printf 'PASS: target Runtime/API layout and native source compile; default-off source has no symbols\n'
