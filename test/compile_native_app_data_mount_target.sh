#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?PlatformIO packages directory required}"
out="${2:?Evidence directory required}"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
cxx="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
nm="${cxx%g++}nm"
mkdir -p "$out"
includes=(-I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$sdk/qio_opi/include" -I"$sdk/include/freertos/include/esp_additions" -I"$sdk/include/freertos/include/esp_additions/freertos" -I"$sdk/include/freertos/port/xtensa/include/freertos")
while IFS= read -r directory; do includes+=(-I"$directory"); done < <(find "$sdk/include" -type d \( -name include -o -name platform_include -o -path '*/include/esp32s3' -o -path '*/include/xtensa' -o -path '*/port/xtensa/include' \) | sort)
flags=(-std=gnu++17 -Wall -Wextra -Werror -Wno-misleading-indentation -Os -mlongcalls -fno-exceptions -fno-rtti -fstack-usage -DESP_PLATFORM -DRISC_PAIRED_APP_DATA)
echo '74c97ecd4b9f2763d7a888b1947638b9188c7310b3815641fcf16fd548342199  '"$sdk/lib/libesp_littlefs.a" | sha256sum -c -
echo '09b7ecccbd4960d49aa9aacc57662c7982221c3f49bc500c663bf117afe78fdd  '"$sdk/qio_opi/include/sdkconfig.h" | sha256sum -c -
"$cxx" --version
for name in NativeAppDataMount NativeAppData; do
 "$cxx" "${flags[@]}" "${includes[@]}" -DRISC_NATIVE_APP_DATA_MOUNT=1 -c "$root/src/ports/esp32s3/$name.cpp" -o "$out/$name-on.o"
 "$cxx" "${flags[@]}" "${includes[@]}" -c "$root/src/ports/esp32s3/$name.cpp" -o "$out/$name-off.o"
 "$nm" -u "$out/$name-on.o" > "$out/$name-on.nm"
done
[[ -z "$("$nm" "$out/NativeAppDataMount-off.o")" ]]
for source in old new; do
 if [[ "$source" == old ]]; then git -C "$root" show 2d362e8958f2938a3b6ad290b2cb2c75744d8f83:src/ports/esp32s3/NativeAppData.cpp > "$out/parity.cpp"; else cp "$root/src/ports/esp32s3/NativeAppData.cpp" "$out/parity.cpp"; fi
 "$cxx" "${flags[@]}" "${includes[@]}" -I"$root/src/ports/esp32s3" -c "$out/parity.cpp" -o "$out/parity-$source.o"
done
cmp "$out/parity-old.o" "$out/parity-new.o"
sha256sum "$out"/*.o "$sdk/lib/libesp_littlefs.a" "$sdk/qio_opi/include/sdkconfig.h"
cat "$out"/*.su
printf 'PASS: pinned target compile; empty default-off binding; NativeAppData default-off byte-identical\n'
