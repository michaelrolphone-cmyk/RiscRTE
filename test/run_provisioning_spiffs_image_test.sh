#!/usr/bin/env bash
# Host-only compact-image proof. No downloads, devices, formatting, or publication.
# SPIFFS sources must be from pellepl/spiffs at the ESP-IDF v4.4.7 submodule:
# 0dbb3f71c5f6fae3747a9d935372773762baf852. They are intentionally not vendored.
# https://github.com/espressif/esp-idf/tree/v4.4.7/components/spiffs
# https://github.com/pellepl/spiffs/tree/0dbb3f71c5f6fae3747a9d935372773762baf852/src
# Usage: script SPIFFS_ROOT ESP32S3_SDK_ROOT IMAGE STORE_DIR [OFFSET PARTITION_BYTES]
set -euo pipefail
[[ "$#" == 4 || "$#" == 6 ]] || { echo "usage: $0 SPIFFS_ROOT ESP32S3_SDK_ROOT IMAGE STORE_DIR [OFFSET PARTITION_BYTES]" >&2; exit 2; }
repo="$(cd "$(dirname "$0")/.." && pwd)"
spiffs="$(cd "$1" && pwd)"; sdk="$(cd "$2" && pwd)"
image="$(realpath "$3")"; store="$(realpath "$4")"
extra=(); if [[ "$#" == 6 ]]; then extra=("$5" "$6"); fi
# Compare official Git blob identities so an unrelated SPIFFS version cannot
# silently claim qualification with these geometry/configuration assumptions.
while read -r sha file; do
 [[ "$(git hash-object "$spiffs/src/$file")" == "$sha" ]] || { echo "Wrong pinned source: $file" >&2; exit 2; }
done <<'HASHES'
58e49fbc24524a9465b463700498a15d519eb0dd spiffs.h
e7cd4b7376c4c6c547f59c6a7034ec40f28fafdc spiffs_cache.c
8c7812ba156e9ecc96504dc35270a26d020cf6b4 spiffs_check.c
db1af4ccf6fa1239359d4df1e93fe92ea6db2152 spiffs_gc.c
7b3e0ae435f5ad10398a00913d78e0b8f942baf1 spiffs_hydrogen.c
ab5cde12d3882660432c7183b0bef3db6c09c9c4 spiffs_nucleus.c
68191361a8d7ce39c89ba3ada9c6bb1e6479a0d6 spiffs_nucleus.h
HASHES
[[ "$(sha256sum "$sdk/include/spiffs/include/spiffs_config.h" | cut -d' ' -f1)" == a32725ddcd9c77bada76bb997c7960a74cfb7bfdb2b586a6a88fe38b89e92c6e ]] || { echo 'Wrong target SPIFFS config' >&2; exit 2; }
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
cp "$sdk/include/spiffs/include/spiffs_config.h" "$build/spiffs_config.h"
grep '^#define CONFIG_SPIFFS_' "$sdk/qio_opi/include/sdkconfig.h" > "$build/sdkconfig.h"
[[ "$(sha256sum "$build/sdkconfig.h" | cut -d' ' -f1)" == 042f413a48e4889ea8a723c767472bce2515ae6ec93b929d047594b12c8ca657 ]] || { echo 'Wrong target SPIFFS sdkconfig' >&2; exit 2; }
printf '#pragma once\n#define ESP_LOGD(...) ((void)0)\n' > "$build/esp_log.h"
printf '#pragma once\n#ifdef __cplusplus\n#define ESP_STATIC_ASSERT static_assert\n#else\n#define ESP_STATIC_ASSERT _Static_assert\n#endif\n' > "$build/esp_assert.h"
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
for name in spiffs_cache spiffs_check spiffs_gc spiffs_hydrogen spiffs_nucleus; do
 # The target on-disk format deliberately stores 16-bit indexes at odd byte
 # offsets (SPIFFS_ALIGNED_OBJECT_INDEX_TABLES=0). Exclude alignment-only UBSan
 # in unchanged upstream C; retain ASan/all other UBSan and full app UBSan.
 cc "${san[@]}" -fno-sanitize=alignment -std=c11 -O2 -I"$build" -I"$spiffs/src" -c "$spiffs/src/$name.c" -o "$build/$name.o"
done
c++ "${san[@]}" -std=c++17 -O2 -I"$build" -I"$spiffs/src" -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 "$repo/test/provisioning_spiffs_image_test.cpp" "$repo/src/runtime/provisioning/StoreFiles.cpp" "$repo/src/runtime/provisioning/Profile.cpp" \
 "$build"/*.o -Wl,--wrap=fopen,--wrap=fread,--wrap=fclose,--wrap=stat,--wrap=lstat,--wrap=remove,--wrap=opendir,--wrap=readdir,--wrap=closedir -lcrypto -o "$build/test"
"$build/test" "$image" "$store" "${extra[@]}"
