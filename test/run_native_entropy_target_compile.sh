#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
: "${ENTROPY_TARGET_PREFIX:?Set the installed Xtensa ESP32-S3 tool prefix, ending in xtensa-esp32s3-elf-}"
: "${ENTROPY_IDF_INCLUDE:?Set the installed ESP-IDF esp_hw_support/include directory}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
incs=(-I"$repo/src" -I"$repo/sdk/driver" -I"$ENTROPY_IDF_INCLUDE")
"${ENTROPY_TARGET_PREFIX}gcc" --version | head -1
"${ENTROPY_TARGET_PREFIX}gcc" -std=c11 -Wall -Wextra -Werror "${incs[@]}" \
 -c "$repo/test/entropy_headers_abi.c" -o "$build/abi.o"
"${ENTROPY_TARGET_PREFIX}g++" -std=gnu++17 -Wall -Wextra -Werror -Os \
 -fno-exceptions -fno-rtti -fstack-usage "${incs[@]}" \
 -c "$repo/test/native_entropy_target_compile.cpp" -o "$build/entropy.o"
"${ENTROPY_TARGET_PREFIX}nm" -u "$build/entropy.o" > "$build/undefined.txt"
cat "$build/undefined.txt" "$build/entropy.su"
# The backend must not acquire radio/ADC/storage or allocate native memory.
awk '$2 != "esp_fill_random" && $2 != "memcpy" && $2 != "memset" { bad=1 } END { exit bad }' "$build/undefined.txt"
echo 'Native entropy ESP32-S3 C ABI and production SDK object compile PASS (no firmware link or device execution)'
