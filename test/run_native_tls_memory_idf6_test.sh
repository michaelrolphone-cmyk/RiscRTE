#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${IDF_PATH:-$repo/../esp-idf}"
mbed="$idf/components/mbedtls"
[[ -f "$mbed/port/esp_mem.c" ]] || { echo "Set IDF_PATH to the selected ESP-IDF6 checkout" >&2;exit 1; }
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-DMBEDTLS_PLATFORM_MEMORY -Wall -Wextra -Werror)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
include=(-I"$repo/test/native_http_idf6_shim" -I"$idf/components/esp_common/include"
 -I"$mbed/mbedtls/include" -I"$mbed/mbedtls/tf-psa-crypto/include"
 -I"$mbed/mbedtls/tf-psa-crypto/drivers/builtin/include" -I"$mbed/port/include" -I"$repo/src")
# Build the real SDK allocator with the supported PSRAM-only startup policy.
"${CC:-cc}" -std=c11 "${flags[@]}" "${san[@]}" "${include[@]}" -DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -c "$mbed/port/esp_mem.c" -o "$build/esp_mem.o"
for policy in external internal default custom iram override internal-zero no-platform;do
  config=(-DRISC_TEST_POLICY_REJECTED=1)
  case "$policy" in
    external) config=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1);;
    internal) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -DCONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1);;
    default) ;;
    custom) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -DCONFIG_MBEDTLS_CUSTOM_MEM_ALLOC=1);;
    iram) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -DCONFIG_MBEDTLS_IRAM_8BIT_MEM_ALLOC=1);;
    override) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -DMBEDTLS_PLATFORM_CALLOC_MACRO=calloc -DMBEDTLS_PLATFORM_FREE_MACRO=free);;
    no-platform) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -UMBEDTLS_PLATFORM_MEMORY);;
    internal-zero) config+=(-DCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1 -DCONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=0);;
  esac
  "${CXX:-c++}" -std=c++17 "${flags[@]}" "${san[@]}" "${include[@]}" "${config[@]}" "$repo/test/native_tls_memory_idf6_test.cpp" "$repo/test/native_tls_memory_other.cpp" "$build/esp_mem.o" -o "$build/test"
  "$build/test" success
  if [[ "$policy" == external ]];then "$build/test" no-psram;fi
done
