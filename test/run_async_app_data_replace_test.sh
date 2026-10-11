#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=undefined -fno-sanitize-recover=all);fi
c++ -std=c++17 -Wall -Wextra -Werror -pthread "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" "$repo/test/async_app_data_replace_test.cpp" -o "$build/test"
"$build/test"
