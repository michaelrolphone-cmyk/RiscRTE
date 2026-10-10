#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/app" -I"$root/sdk/hardware" -I"$root/test/native_sdmmc_shim" "$root/test/native_sdmmc_test.cpp" -o "$build/test"
for case in happy allocation-failure init-failure slot-failure card-failure read-failure write-failure sync-failure close-failure reset-failure;do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$case";done
