#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_sd_port_target.sh PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide output directory}";mkdir -p "$out"
tool="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
flags=(-std=gnu++17 -D_GNU_SOURCE -DESP_PLATFORM -DRISC_NATIVE_STORAGE_CUSTODY=1 -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti
 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src"
 -I"$sdk/include/heap/include" -I"$sdk/include/esp_common/include" -I"$sdk/qio_opi/include")
"$tool-g++" --version
for layout in legacy cohort; do
 selected=();if [[ "$layout" == cohort ]];then selected=(-DRISC_RUNTIME_METADATA_PSRAM=1 -DRISC_PROVIDER_CAPACITY=40 -DRISC_INSTALLED_APP_CAPACITY=64);fi
 "$tool-g++" "${flags[@]}" "${selected[@]}" -c "$root/test/native_sd_port_target_probe.cpp" -o "$out/probe-$layout.o"
 printf '%s\n' "$layout";"$tool-nm" -S "$out/probe-$layout.o" | grep 'native_sd_.*_size'
 sha256sum "$out/probe-$layout.o"
done
