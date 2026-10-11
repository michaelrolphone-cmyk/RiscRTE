#!/usr/bin/env bash
# Header/object qualification only. No SDK ABI selection, link, image or flash.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${TARGET_CXX:?Set TARGET_CXX to xtensa-esp32s3-elf-g++}"
core="${CORE_ROOT:-$root}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -I"$core" -I"$root")
"$TARGET_CXX" "${flags[@]}" -c "$root/test/native_rebase_candidate_compile.cpp" -o "$build/receiver.o"
echo 'Native generic receiver: strict no-exception/no-RTTI target object compile PASS'
"$TARGET_CXX" "${flags[@]}" -I"$root/src" -I"$root/sdk/app" -c "$root/test/native_rebase_store_compile.cpp" -o "$build/store.o"
echo 'Experimental retained native store: strict target object compile PASS'
if [[ -n "${SYSTEM_ROOT:-}" ]];then
 "$TARGET_CXX" "${flags[@]}" -I"$SYSTEM_ROOT" -I"$SYSTEM_ROOT/lib/PortableApps/include" -I"$SYSTEM_ROOT/lib/RemoteFiles/vendor/tinydtls_sha2" -c "$root/test/native_rebase_wifi_stage_compile.cpp" -o "$build/wifi.o"
 echo 'Actual app-owned Wi-Fi Stage receiver bridge: strict target object compile PASS'
fi
"$TARGET_CXX" --version | head -1
