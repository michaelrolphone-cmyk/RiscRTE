#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
core="${CORE_ROOT:-$root}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -I"$core" -I"$root" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/sdk/app" "$root/test/native_rebase_candidate_test.cpp" -o "$build/test"
"$build/test"
