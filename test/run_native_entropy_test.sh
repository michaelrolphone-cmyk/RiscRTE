#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror)
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then
 san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
if [[ "${TSAN:-0}" == 1 ]];then
 san=(-fsanitize=thread -fno-omit-frame-pointer -g)
fi
incs=(-I"$repo/src" -I"$repo/sdk/driver")
cc -std=c11 "${flags[@]}" "${incs[@]}" -c "$repo/test/entropy_headers_abi.c" -o "$build/abi.o"
c++ -std=c++17 "${flags[@]}" "${san[@]}" "${incs[@]}" -I"$repo/test/native_entropy_shim" \
 "$repo/test/native_entropy_test.cpp" -pthread -o "$build/native"
"$build/native"
