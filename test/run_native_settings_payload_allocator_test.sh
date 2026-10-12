#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?Output directory required}";mkdir -p "$out"
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -DESP_PLATFORM -DBOARD_XTEINK_X4_PRO -DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -I"$root" -I"$root/src" -I"$root/test/native_settings_payload_shim" "$root/test/native_settings_payload_allocator_test.cpp" "$root/src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp" -o "$out/test"
"$out/test"
