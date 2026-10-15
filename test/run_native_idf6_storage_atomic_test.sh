#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
sanitize=(); if [[ "${SANITIZE:-0}" == 1 ]]; then sanitize=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${sanitize[@]}" -DRISC_NATIVE_ESP_IDF=1 \
 -I"$root/test/native_storage_atomic_shim" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 "$root/test/native_idf6_storage_atomic_test.cpp" -o "$build/test"
"$build/test"
