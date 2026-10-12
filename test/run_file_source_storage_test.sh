#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${OUTPUT_DIR:-$(mktemp -d)}";mkdir -p "$build/data"
flags=(-g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -std=c++17)
if [[ ${SANITIZE:-0} == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
c++ "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" "$repo/test/file_source_storage_test.cpp" -Wl,--wrap=close,--wrap=write,--wrap=unlink,--wrap=open,--wrap=read,--wrap=__read_chk -o "$build/test"
"$build/test" "$build/data"
