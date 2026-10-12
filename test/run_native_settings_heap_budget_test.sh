#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?Output directory required}";mkdir -p "$out"
# AppDataFiles requires an absolute filesystem root. Normalize caller-relative
# output paths, including the exact GitHub Actions invocation.
out="$(cd "$out" && pwd)"
flags=(-std=c++17 -fno-access-control -O1 -g -Wall -Wextra -Werror -Wno-unused-function)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/test/native_nvs_shim" "$root/test/native_settings_heap_budget_test.cpp" "$root/src/runtime/storage/AppDataFiles.cpp" -lcrypto -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$out/heap-budget"
"$out/heap-budget" "$out/private-fixture"
