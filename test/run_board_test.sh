#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/test/board_test.cpp" -o "$build/test"
"$build/test" "$@"
