#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g -no-pie)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cc "${flags[@]}" -I"$repo/sdk/app" "$repo/test/runtime_checkpoint_api_test.c" -o "$build/test"
"$build/test"
echo 'Optional Runtime checkpoint prefix compatibility PASS'
