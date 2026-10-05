#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -g); fi
c++ "${san[@]}" -DRISC_PAIRED_BANKS=1 -std=c++17 -Wall -Wextra -Werror \
 -I"$repo/test/sntp_stubs" -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 -I"$repo/sdk/hardware" -I"$repo/sdk/app" -I"$repo/sdk/driver" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/ports/esp32s3/NativeSntp.cpp" "$repo/test/native_sntp_test.cpp" -o "$build/test"
for mode in success timeout cancel range stop-fail busy invalid unknown; do "$build/test" "$mode"; done
