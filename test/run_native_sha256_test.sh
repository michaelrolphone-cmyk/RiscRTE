#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
psa="$idf/components/mbedtls/mbedtls/tf-psa-crypto"
test -f "$psa/include/psa/crypto.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -DRISC_NATIVE_ESP_IDF=1)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" -I"$psa/include" -I"$psa/drivers/builtin/include" \
  "$repo/test/native_sha256_test.cpp" -lcrypto -o "$build/test"
"$build/test"
