#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" \
  "$repo/src/runtime/storage/ScopedUserVolume.cpp" \
  "$repo/test/scoped_user_volume_test.cpp" -o "$build/scoped-user-volume-test"
"$build/scoped-user-volume-test"
