#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
c++ "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/runtime_file_image_test.cpp" -o "$build/test"
"$build/test"
