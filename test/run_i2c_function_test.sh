#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)";trap 'rm -rf "$tmp"' EXIT
flags=()
if [[ "${SANITIZE:-0}" == 1 ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src" "$root/src/bootstrap/Board.cpp" "$root/src/bootstrap/Json.cpp" "$root/test/i2c_function_test.cpp" -o "$tmp/test"
"$tmp/test"
