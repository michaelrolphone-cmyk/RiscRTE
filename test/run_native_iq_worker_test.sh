#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for sdk in legacy idf6; do
 extra=();if [[ "$sdk" == idf6 ]];then extra=(-DRISC_NATIVE_ESP_IDF=1);fi
 "${CXX:-c++}" "${flags[@]}" "${extra[@]}" -fno-pie -no-pie -I"$repo/test/native_iq_shim" -I"$repo/sdk/driver" -I"$repo/src" "$repo/test/native_iq_worker_test.cpp" -Wl,-T,"$repo/test/native_iq_shim/memory.ld" -o "$build/native-iq-worker-$sdk"
 "$build/native-iq-worker-$sdk" "$@"
done
