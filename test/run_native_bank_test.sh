#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
cc "${san[@]}" -std=c11 -I"$repo/test/native_bank_stubs" -I"$repo/lib/elf_loader/include" -c "$repo/lib/elf_loader/src/esp_elf_validate.c" -o "$build/validate.o"
extra=(-DARDUINO_USB_CDC_ON_BOOT=1 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER="${NATIVE_DIAGNOSTIC_OBSERVER:-0}");if [[ "${APP_DATA_TEST:-0}" == 1 ]];then extra+=(-DRISC_PAIRED_APP_DATA=1);fi
native_objects=()
if [[ -z "${BOOT_BASELINE_REF:-}" ]];then
 extra+=(-DRISC_NATIVE_PROVIDER_ADMISSION_TEST=1 -DRISC_NATIVE_BANK_PROVIDER_TEST=1 -Wl,--wrap=fread,--wrap=fgetc)
 for source in esp_privileged_imports esp_privileged_manifest_imports;do
  cc "${san[@]}" -std=gnu11 -Wall -Wextra -Werror -I"$repo/test/native_bank_stubs" -I"$repo/lib/elf_loader/include" -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$source.o"
  native_objects+=("$build/$source.o")
 done
 if [[ "${NATIVE_PROVIDER_TEST_SELECTION:-0}" == 1 ]];then
  extra+=(-DRISC_NATIVE_BANK_TEST_SELECT_POLICY=1 '-DRISC_NATIVE_PROVIDER_POLICY_HEADER="native_bank_provider_selection.h"')
 fi
 if [[ "${NATIVE_EXPORT_TEST_SELECTION:-0}" == 1 ]];then
  extra+=(-DRISC_NATIVE_BANK_TEST_SELECT_EXPORT_POLICY=1 '-DRISC_NATIVE_APP_DATA_EXPORT_POLICY_HEADER="native_bank_app_data_export_selection.h"')
 fi
fi
if [[ -n "${BOOT_BASELINE_REF:-}" ]];then
 git -C "$repo" show "$BOOT_BASELINE_REF:src/ports/esp32s3/NativeBankStore.cpp" > "$build/NativeBankStore.cpp"
 extra+=("-DRISC_NATIVE_BANK_SOURCE=\"$build/NativeBankStore.cpp\"" -DRISC_TEST_BOOT_SCANS=1 -I"$repo/src/ports/esp32s3")
 version="$(git -C "$repo" show "$BOOT_BASELINE_REF:platformio.ini" | python3 -c 'import configparser,sys;c=configparser.ConfigParser();c.read_string(sys.stdin.read());print(c["riscrte"]["version"])')"
 extra+=("-DRISC_BUILD_VERSION=\"$version\"")
elif [[ -n "${PRODUCT_STORE:-}${BOOT_PATH_FIRMWARE:-}" ]];then
 version="$(python3 -c 'import configparser; c=configparser.ConfigParser();c.read("platformio.ini");print(c["riscrte"]["version"])')"
 [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
 extra+=("-DRISC_BUILD_VERSION=\"$version\"")
fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/cohort_native_app.c" -o "$build/cohort-native.elf"
c++ -rdynamic -Wl,--wrap=fopen,--wrap=fclose,--wrap=opendir,--wrap=stat,--wrap=lstat "${san[@]}" "${extra[@]}" -DRISC_PAIRED_BANKS=1 -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
 -I"$repo/test" -I"$repo/test/native_bank_stubs" -I"$repo/test/drivers/stubs" -I"$repo/lib/elf_loader/include" \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/provisioning/BootstrapInput.cpp" "$repo/src/runtime/provisioning/Coordinator.cpp" \
 "$repo/src/runtime/provisioning/StoreFiles.cpp" "$repo/src/runtime/provisioning/Profile.cpp" \
 "$repo/src/runtime/update/PairedBank.cpp" "$repo/src/runtime/update/StoreAudit.cpp" "$repo/test/native_bank_test.cpp" "$build/validate.o" "${native_objects[@]}" -lcrypto -ldl -o "$build/test"
if [[ "${NATIVE_EXPORT_ADMISSION_ONLY:-0}" == 1 ]];then
 "$build/test" native-export-admission "$build/export-admission"
 if [[ "${NATIVE_EXPORT_TEST_SELECTION:-0}" == 1 ]];then
  "$build/test" native-export-close "$build/export-close"
 fi
 exit 0
fi
if [[ "${NATIVE_PROVIDER_ADMISSION_ONLY:-0}" == 1 ]];then
 "$build/test" native-provider-admission "$build/provider-admission"
 "$build/test" native-provider-custody-owner "$build/provider-custody-owner"
 "$build/test" native-provider-custody-storage "$build/provider-custody-storage"
 for mode in owner unsafe close;do
  "$build/test" "native-provider-second-$mode" "$build/provider-second-$mode"
 done
 if [[ "${NATIVE_PROVIDER_TEST_SELECTION:-0}" == 1 ]];then
  "$build/test" native-provider-custody-provision "$build/provider-custody-provision"
  for mode in owner unsafe close;do
   "$build/test" "native-provider-second-provision-$mode" "$build/provider-second-provision-$mode"
  done
 fi
 exit 0
fi
if [[ "${BOOT_PATH_ONLY:-0}" == 1 || -n "${BOOT_BASELINE_REF:-}" ]];then
 "$build/test" boot-cost "$BOOTLOADER_FILE" ${BOOT_PATH_FIRMWARE:+"$BOOT_PATH_FIRMWARE"}
 exit 0
fi
if [[ -n "${PRODUCT_STORE:-}" ]];then
 "$build/test" provision-product "$BOOTLOADER_FILE" "$build/product-stage" "$PRODUCT_STORE" "$PRODUCT_FIRMWARE"
 exit 0
fi
"$build/test" markers
"$build/test" native-provider-admission "$build/provider-admission"
"$build/test" boot-records
"$build/test" blank-record
"$build/test" diagnostic-candidate "$build/diagnostic-candidate"
"$build/test" diagnostic-candidate-absent "$build/diagnostic-candidate-absent"
if [[ -n "${BOOTLOADER_FILE:-}" ]]; then
 for mode in success odd-chunks interrupt offline-consumed corrupt length download-fail write-fail readback-fail readback-corrupt bad-board bad-elf native-mismatch bad-magic bad-name bad-capacity free-page-programmed extra-file file-mismatch mount-fail unmount-retained admission-close selection-unknown receipt-fail; do
  "$build/test" "image-$mode" "$BOOTLOADER_FILE" "$build/image-$mode"
 done
 "$build/test" boot-cost "$BOOTLOADER_FILE"
 if [[ -n "${SEED_DIRECTORY:-}" ]]; then "$build/test" provision-seed "$BOOTLOADER_FILE" "$build/seed-stage" "$SEED_DIRECTORY"; fi
 for mode in cohort cohort-receipt cohort-legacy-receipt cohort-live-close; do
  mkdir "$build/$mode";cp "$build/cohort-native.elf" "$build/$mode/cohort-native.elf"
  "$build/test" "$mode" "$BOOTLOADER_FILE" "$build/$mode"
 done
 for mode in boot unscanned-store bad-layout restart restart-unknown; do "$build/test" "$mode" "$BOOTLOADER_FILE"; done
 for mode in provision provision-abort provision-corrupt provision-unknown provision-admission provision-close-retained; do "$build/test" "$mode" "$BOOTLOADER_FILE" "$build/$mode"; done
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" "$repo/test/fixtures/bootstrap_fallback.c" -o "$build/installed-default.elf"
 for mode in metadata-close-retained receipt-matched receipt-changed receipt-corrupt receipt-pending rolled-back changed-profile changed-source legacy-attempt attempt-malformed attempt-torn attempt-bank-mismatch attempt-store-mismatch attempt-firmware-mismatch attempt-format attempt-read-fail attempt-write-fail attempt-write-torn attempt-write-readback pending-bank confirmed-bank absent invalid profile-invalid no-time unchanged time-unavailable time-stale time-future time-invalid time-timeout time-pending time-retained download-fail http-open-fail corrupt length-mismatch http-retained radio-retained match-close-retained native-unsafe oom success selection-unknown; do
  "$build/test" "bootstrap-$mode" "$BOOTLOADER_FILE" "$build/bootstrap-$mode" "$build/installed-default.elf"
 done
else
 echo 'Bootloader-backed happy/unscanned-store/layout tests require BOOTLOADER_FILE from a verified paired target build.'
fi
