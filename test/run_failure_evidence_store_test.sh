#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O1 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" \
  "$repo/test/failure_evidence_store_test.cpp" -o "$build/test"
"$build/test"
