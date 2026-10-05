#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
cc "${san[@]}" -std=c11 -I"$repo/test/native_bank_stubs" -I"$repo/lib/elf_loader/include" -c "$repo/lib/elf_loader/src/esp_elf_validate.c" -o "$build/validate.o"
extra=();if [[ "${APP_DATA_TEST:-0}" == 1 ]];then extra=(-DRISC_PAIRED_APP_DATA=1);fi
c++ "${san[@]}" "${extra[@]}" -DRISC_PAIRED_BANKS=1 -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
 -I"$repo/test/native_bank_stubs" -I"$repo/test/drivers/stubs" -I"$repo/lib/elf_loader/include" \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/update/PairedBank.cpp" "$repo/src/runtime/update/StoreAudit.cpp" "$repo/test/native_bank_test.cpp" "$build/validate.o" -lcrypto -ldl -o "$build/test"
"$build/test" markers
"$build/test" unknown-loader
if [[ -n "${BOOTLOADER_FILE:-}" ]]; then
 for mode in boot bad-store bad-layout restart restart-unknown; do "$build/test" "$mode" "$BOOTLOADER_FILE"; done
else
 echo 'Bootloader-backed happy/bad-store/layout tests require BOOTLOADER_FILE from a verified paired target build.'
fi
