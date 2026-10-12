#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_settings_worker_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide an output directory}"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
compiler="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
nm="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm"
test -x "$compiler"; mkdir -p "$out"
includes=(-I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$sdk/qio_opi/include" -I"$sdk/include/freertos/include/esp_additions" -I"$sdk/include/freertos/include/esp_additions/freertos" -I"$sdk/include/freertos/port/xtensa/include/freertos")
while IFS= read -r directory; do includes+=(-I"$directory"); done < <(find "$sdk/include" -type d \( -name include -o -name platform_include -o -path '*/include/esp32s3' -o -path '*/include/xtensa' -o -path '*/port/xtensa/include' \) | sort)
flags=(-std=gnu++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -DESP_PLATFORM -DRISC_NATIVE_SETTINGS_WORKER=1)
"$compiler" --version
"$compiler" "${flags[@]}" "${includes[@]}" -c "$root/src/ports/esp32s3/NativeSettingsWorker.cpp" -o "$out/native-settings-worker.o"
"$compiler" "${flags[@]}" "${includes[@]}" -c "$root/test/native_settings_worker_target_probe.cpp" -o "$out/native-settings-worker-probe.o"
"$compiler" "${flags[@]}" "${includes[@]}" -c "$root/test/native_settings_worker_layout_probe.cpp" -o "$out/native-settings-worker-layout.o"
"$compiler" -std=gnu++17 -Wall -Wextra -Werror -DESP_PLATFORM "${includes[@]}" -c "$root/src/ports/esp32s3/NativeSettingsWorker.cpp" -o "$out/native-settings-worker-default-off.o"
"$nm" -S "$out/native-settings-worker-probe.o" | grep native_settings_coordinator_size
"$nm" -S "$out/native-settings-worker-layout.o" | grep -E 'native_settings_(binding|registry|two_bindings)_'
"$nm" -u "$out/native-settings-worker.o" | grep -E 'xTaskCreateStaticPinnedToCore|nativeBackgroundResource|heap_caps_malloc|vTaskDelay'
if "$nm" -u "$out/native-settings-worker.o" | grep -E 'vTaskDelete|esp_vfs_littlefs|nvs_flash_init'; then echo 'Unexpected task deletion or mount/init dependency' >&2; exit 1; fi
if test -n "$("$nm" "$out/native-settings-worker-default-off.o")"; then echo 'Default-off object is not empty' >&2; exit 1; fi
sha256sum "$out"/*.o
