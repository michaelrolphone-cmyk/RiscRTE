#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_storage_custody_target.sh PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide output directory}";mkdir -p "$out"
tool="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf"
sdk="$packages/framework-arduinoespressif32/tools/sdk/esp32s3"
flags=(-std=gnu++17 -D_GNU_SOURCE -DESP_PLATFORM -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti
 -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src"
 -I"$sdk/include/heap/include" -I"$sdk/include/esp_common/include" -I"$sdk/qio_opi/include")
"$tool-g++" --version
for layout in legacy cohort; do
 selected=();if [[ "$layout" == cohort ]];then selected=(-DRISC_RUNTIME_METADATA_PSRAM=1 -DRISC_PROVIDER_CAPACITY=40 -DRISC_INSTALLED_APP_CAPACITY=64);fi
 for feature in 0 1;do
  name="$layout-$feature"
  "$tool-g++" "${flags[@]}" "${selected[@]}" -DRISC_NATIVE_STORAGE_CUSTODY="$feature" -c "$root/src/ports/esp32s3/CpuPort.cpp" -o "$out/CpuPort-$name.o"
  "$tool-g++" "${flags[@]}" "${selected[@]}" -DRISC_NATIVE_STORAGE_CUSTODY="$feature" -c "$root/test/native_storage_custody_target_probe.cpp" -o "$out/probe-$name.o"
  printf '%s ' "$name";"$tool-nm" -S "$out/probe-$name.o" | grep native_storage_port_size
  sha256sum "$out/CpuPort-$name.o" "$out/probe-$name.o"
 done
done
