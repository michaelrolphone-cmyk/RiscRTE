#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic "${flags[@]}" \
  -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/app" \
  "$root/test/storage_files_adapter_test.cpp" -o "$build/storage-files-test"
"$build/storage-files-test"
# The app-facing header must remain directly consumable by plain C clients.
"${CC:-cc}" -x c -std=c11 -Wall -Wextra -Werror -pedantic -I"$root/sdk/app" \
  -o "$build/storage-files-c-api" - <<'C_SOURCE'
#include <RiscStorageFilesV1.h>
_Static_assert(RISC_STORAGE_FILES_HANDLE_INVALID == 0, "invalid token ABI");
_Static_assert(RISC_STORAGE_FILES_IO_MAX == 4096, "bounded transfer ABI");
_Static_assert(sizeof(((risc_storage_files_entry_v1*)0)->name) == 128, "entry name ABI");
_Static_assert(RISC_FILES_OK == 0 && RISC_FILES_NOT_FOUND == 1 && RISC_FILES_END == 2, "status ABI");
int main(void) {
    risc_storage_files_v1 api = {0};
    api.api_version = RISC_STORAGE_FILES_API_V1;
    api.struct_size = sizeof(api);
    return api.api_version != 1u;
}
C_SOURCE
"$build/storage-files-c-api"
printf 'storage files C ABI: PASS\n'
