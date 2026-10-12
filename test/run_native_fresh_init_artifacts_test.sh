#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
wraps=(-Wl,--wrap=lstat,--wrap=fstat,--wrap=fstatat,--wrap=open,--wrap=openat,--wrap=fdopendir,--wrap=readdir,--wrap=read,--wrap=close,--wrap=closedir)
"${CXX:-c++}" "${flags[@]}" -I"$root" "$root/test/native_fresh_init_artifacts_test.cpp" "${wraps[@]}" -o "$build/test"
"$build/test" "$build/native"
"${CXX:-c++}" "${flags[@]}" -Wno-unused-function -DESP_PLATFORM -I"$root" "$root/test/native_fresh_init_artifacts_test.cpp" "${wraps[@]}" -o "$build/unsupported"
"$build/unsupported" "$build/unsupported-native"
