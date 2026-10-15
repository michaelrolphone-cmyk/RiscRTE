#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -DESP_PLATFORM -DRISC_NATIVE_ESP_IDF=1 -DRISC_NATIVE_SETTINGS_WORKER=1)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-address,undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
"${CXX:-c++}" "${flags[@]}" -I"$root/test/native_settings_atomic_shim" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/test/native_settings_worker_shim" -I"$root/test/native_nvs_shim" "$root/test/native_settings_internal_atomics_test.cpp" "$root/src/runtime/storage/AppDataFiles.cpp" -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
for mode in placement registration single dual blocked1 blocked6 retained1 retained6; do "$build/test" "$mode" "$build/$mode"; done
