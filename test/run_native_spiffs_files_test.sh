#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
idf="${1:?usage: run_native_spiffs_files_test.sh PATH_TO_PINNED_IDF}"
[[ "$(git -C "$idf" rev-parse HEAD)" == fff9895c82d744c7237be8847347bdd1b07c6643 ]]
[[ "$(git -C "$idf/components/spiffs/spiffs" rev-parse HEAD)" == ad902cadceb39d0825a97e25ecb5867641f606ba ]]
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$repo/test/extract_spiffs_vfs_fixture.py" "$idf/components/spiffs/esp_spiffs.c" "$build/spiffs_vfs_callbacks.inc"
cat > "$build/sdkconfig.h" <<'CONFIG'
#define CONFIG_SPIFFS_GC_MAX_RUNS 10
#define CONFIG_SPIFFS_OBJ_NAME_LEN 128
#define CONFIG_SPIFFS_META_LENGTH 0
#define CONFIG_SPIFFS_PAGE_SIZE 256
#define CONFIG_SPIFFS_USE_MAGIC 1
#define CONFIG_SPIFFS_USE_MAGIC_LENGTH 1
CONFIG
printf '#pragma once\n' > "$build/esp_log.h"
printf '#pragma once\n#define ESP_STATIC_ASSERT _Static_assert\n' > "$build/esp_assert.h"
flags=(-g -fno-omit-frame-pointer)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all);fi
spiffs="$idf/components/spiffs/spiffs/src"
incs=(-I"$build" -I"$idf/components/spiffs/include" -I"$spiffs")
objects=()
for source in "$spiffs"/spiffs_{cache,check,gc,hydrogen,nucleus}.c "$repo/test/spiffs_vfs_fixture.c";do
 object="$build/$(basename "$source").o"
 # Pinned IDF intentionally disables aligned SPIFFS object index tables.
 # Keep full sanitizers on our bridge/provider; suppress only that known
 # vendor byte-packed index access check in the unmodified SPIFFS C sources.
 vendor_flags=()
 if [[ "$source" == "$spiffs"/* ]];then vendor_flags+=(-fno-sanitize=alignment);fi
 cc -std=c11 -D_DEFAULT_SOURCE "${flags[@]}" "${vendor_flags[@]}" "${incs[@]}" -c "$source" -o "$object"
 objects+=("$object")
done
wraps=()
for name in open close read write lseek fstat stat fsync ftruncate remove mkdir opendir readdir closedir rewinddir telldir seekdir;do wraps+=(-Wl,--wrap="$name");done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DESP_PLATFORM -DRISC_POSIX_FILES_NOREPLACE=0 "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" "$repo/test/native_spiffs_files_test.cpp" "${objects[@]}" "${wraps[@]}" -no-pie -o "$build/test"
"$build/test"
