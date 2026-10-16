#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
mbed="$idf/components/mbedtls"
[[ -f "$mbed/mbedtls/include/mbedtls/ssl.h" ]] || { echo "Set IDF_PATH to the selected ESP-IDF6 checkout" >&2;exit 1; }
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-DMBEDTLS_PLATFORM_MEMORY -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
# Real selected SDK mbedTLS4/PSA declarations, HTTP parser and esp_mem.c. Only ESP-TLS
# transport, heap capabilities and monotonic clock are fault-injected.
include=(-I"$repo/test/native_http_idf6_shim" -I"$idf/components/esp_common/include"
 -I"$mbed/mbedtls/include" -I"$mbed/mbedtls/tf-psa-crypto/include"
 -I"$mbed/mbedtls/tf-psa-crypto/drivers/builtin/include" -I"$mbed/port/include"
 -I"$idf/components/http_parser" -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/test/native_http_shim")
"${CC:-cc}" -std=c11 "${flags[@]}" "${san[@]}" "${include[@]}" -DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -c "$mbed/port/esp_mem.c" -o "$build/esp_mem.o"
"${CC:-cc}" -std=c11 "${san[@]}" "${include[@]}" -c "$idf/components/http_parser/http_parser.c" -o "$build/parser.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" "${san[@]}" "${include[@]}" -DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 "$repo/test/native_http_test.cpp" "$build/parser.o" "$build/esp_mem.o" -Wl,--wrap=close,--wrap=getaddrinfo,--wrap=freeaddrinfo,--wrap=socket,--wrap=fcntl,--wrap=sendto,--wrap=recvfrom -o "$build/native"
"$build/native"
