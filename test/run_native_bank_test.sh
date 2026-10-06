#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
cc "${san[@]}" -std=c11 -I"$repo/test/native_bank_stubs" -I"$repo/lib/elf_loader/include" -c "$repo/lib/elf_loader/src/esp_elf_validate.c" -o "$build/validate.o"
extra=(-DARDUINO_USB_CDC_ON_BOOT=1);if [[ "${APP_DATA_TEST:-0}" == 1 ]];then extra+=(-DRISC_PAIRED_APP_DATA=1);fi
if [[ -n "${PRODUCT_STORE:-}" ]];then
 version="$(python3 -c 'import configparser; c=configparser.ConfigParser();c.read("platformio.ini");print(c["riscrte"]["version"])')"
 [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
 extra+=("-DRISC_BUILD_VERSION=\"$version\"")
fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/cohort_native_app.c" -o "$build/cohort-native.elf"
c++ -rdynamic -Wl,--wrap=fopen,--wrap=fclose,--wrap=opendir,--wrap=stat,--wrap=lstat "${san[@]}" "${extra[@]}" -DRISC_PAIRED_BANKS=1 -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
 -I"$repo/test/native_bank_stubs" -I"$repo/test/drivers/stubs" -I"$repo/lib/elf_loader/include" \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/provisioning/BootstrapInput.cpp" "$repo/src/runtime/provisioning/Coordinator.cpp" \
 "$repo/src/runtime/provisioning/StoreFiles.cpp" "$repo/src/runtime/provisioning/Profile.cpp" \
 "$repo/src/runtime/update/PairedBank.cpp" "$repo/src/runtime/update/StoreAudit.cpp" "$repo/test/native_bank_test.cpp" "$build/validate.o" -lcrypto -ldl -o "$build/test"
if [[ -n "${PRODUCT_STORE:-}" ]];then
 "$build/test" provision-product "$BOOTLOADER_FILE" "$build/product-stage" "$PRODUCT_STORE" "$PRODUCT_FIRMWARE"
 exit 0
fi
"$build/test" markers
"$build/test" unknown-loader
if [[ -n "${BOOTLOADER_FILE:-}" ]]; then
 if [[ -n "${SEED_DIRECTORY:-}" ]]; then "$build/test" provision-seed "$BOOTLOADER_FILE" "$build/seed-stage" "$SEED_DIRECTORY"; fi
 for mode in cohort cohort-receipt cohort-legacy-receipt cohort-live-close; do
  mkdir "$build/$mode";cp "$build/cohort-native.elf" "$build/$mode/cohort-native.elf"
  "$build/test" "$mode" "$BOOTLOADER_FILE" "$build/$mode"
 done
 for mode in boot bad-store bad-layout restart restart-unknown; do "$build/test" "$mode" "$BOOTLOADER_FILE"; done
 for mode in provision provision-abort provision-corrupt provision-unknown provision-admission provision-close-retained; do "$build/test" "$mode" "$BOOTLOADER_FILE" "$build/$mode"; done
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" "$repo/test/fixtures/bootstrap_fallback.c" -o "$build/installed-default.elf"
 for mode in metadata-close-retained receipt-matched receipt-changed receipt-corrupt receipt-pending rolled-back changed-profile changed-source legacy-attempt attempt-malformed attempt-torn attempt-bank-mismatch attempt-store-mismatch attempt-firmware-mismatch attempt-format attempt-read-fail attempt-write-fail attempt-write-torn attempt-write-readback pending-bank confirmed-bank absent invalid profile-invalid no-time unchanged time-unavailable time-stale time-future time-invalid time-timeout time-pending time-retained download-fail http-open-fail corrupt length-mismatch http-retained radio-retained match-close-retained native-unsafe oom success selection-unknown; do
  "$build/test" "bootstrap-$mode" "$BOOTLOADER_FILE" "$build/bootstrap-$mode" "$build/installed-default.elf"
 done
else
 echo 'Bootloader-backed happy/bad-store/layout tests require BOOTLOADER_FILE from a verified paired target build.'
fi
