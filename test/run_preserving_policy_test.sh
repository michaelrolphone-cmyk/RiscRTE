#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
 -Wl,--wrap=fopen,--wrap=fclose,--wrap=fread,--wrap=feof,--wrap=ferror,--wrap=malloc \
 -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/PreservingPolicy.cpp" \
 "$repo/test/preserving_policy_test.cpp" -o "$build/test"
"$build/test" "$build"
