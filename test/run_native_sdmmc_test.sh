#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
for signed_capacity in 0 1;do
 "${CXX:-c++}" "${flags[@]}" -DRISC_SDMMC_TEST_SIGNED_CAPACITY="$signed_capacity" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/app" -I"$root/sdk/hardware" -I"$root/test/native_sdmmc_shim" "$root/test/native_sdmmc_test.cpp" -o "$build/test"
 for case in happy allocation-failure init-failure slot-failure card-failure read-failure write-failure sync-failure close-failure reset-failure zero-capacity capacity-limit;do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$case";done
 if [[ "$signed_capacity" == 1 ]];then ASAN_OPTIONS=detect_leaks=0 "$build/test" negative-capacity;fi
done
