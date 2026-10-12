#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-deprecated-declarations "${flags[@]}" -I"$root" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/app" -I"$root/sdk/hardware" "$root/test/native_settings_scratch_allocation_test.cpp" -lcrypto -o "$build/test"
"$build/test"
