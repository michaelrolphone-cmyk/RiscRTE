#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
extra=();if [[ "${APP_DATA_TEST:-0}" == 1 ]];then extra=(-DRISC_PAIRED_APP_DATA=1);fi
c++ "${san[@]}" "${extra[@]}" -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
 "$repo/src/runtime/update/PairedBank.cpp" "$repo/test/paired_bank_test.cpp" -lcrypto -o "$build/test"
"$build/test"
