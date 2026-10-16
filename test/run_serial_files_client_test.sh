#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
c++ -std=c++17 "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" \
  -no-pie "$repo/src/runtime/serial/Files.cpp" "$repo/test/serial_files_wire.cpp" -o "$build/wire"
PYTHONDONTWRITEBYTECODE=1 python3 "$repo/test/serial_files_client_test.py" "$build/wire"
