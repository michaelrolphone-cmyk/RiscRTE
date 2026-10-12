#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pthread)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
"${CXX:-c++}" "${flags[@]}" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/test/native_nvs_shim" "$root/test/native_settings_coordinator_test.cpp" "$root/src/runtime/storage/AppDataFiles.cpp" -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
"$build/test" "$build/store"
