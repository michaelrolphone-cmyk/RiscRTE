#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
: "${ESP_IDF:?Point ESP_IDF to pristine ESP-IDF 4.4.7 with its pinned mbedTLS submodule}"
[[ "$(git -C "$ESP_IDF" rev-parse HEAD)" == 38eeba213aa695aabfd6d89aa9f5078dbe5a94c3 ]]
[[ -z "$(git -C "$ESP_IDF" status --porcelain --untracked-files=no)" ]]
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cat > "$build/sdkconfig.h" <<CONFIG
#define CONFIG_NVS_ENCRYPTION 1
#define CONFIG_SPI_FLASH_USE_LEGACY_IMPL 1
#define CONFIG_LOG_MAXIMUM_LEVEL 1
#define CONFIG_LOG_TIMESTAMP_SOURCE_RTOS 1
#define CONFIG_IDF_TARGET_LINUX 1
#define CONFIG_NVS_ASSERT_ERROR_CHECK ${NVS_ASSERTIONS:-0}
CONFIG
sha256sum "$repo/src/runtime/provisioning/RequestQueue."{cpp,h} "$repo/src/ports/esp32s3/"{NvsProvisionRequestStorage.h,NativeProvisionRequest.cpp,NvsKeyValue.cpp,NvsKeyValue.h,NvsQuarantine.h,NativeRadio.h,NativeHci.h,NativeRadioIq.h,NvsScalableSettings.h,NvsBootstrapInput.h,OwnerNvsInstaller.cpp} "$repo/test/request_queue_nvs_test.cpp" "$repo/test/fixtures/"request-profile-*.json
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
includes=(-I"$build" -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/sdk/app" -I"$repo/sdk/hardware" -I"$repo/sdk/app" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src")
for path in nvs_flash/private_include nvs_flash/include nvs_flash/src esp_rom/include esp_rom/include/linux log/include nvs_flash/test_nvs_host esp_common/include esp32/include mbedtls/mbedtls/include spi_flash/include hal/include xtensa/include;do includes+=(-I"$ESP_IDF/components/$path");done
includes+=(-I"$ESP_IDF/tools/catch")
flags=("${san[@]}" -DNO_DEBUG_STORAGE -std=c++17 -Wall -DLINUX_TARGET -DCATCH_CONFIG_NO_POSIX_SIGNALS -g -O1 '-DRISC_TEST_FIXTURES="'"$repo/test/fixtures"'"')
objects=()
for file in "$ESP_IDF"/components/nvs_flash/src/{nvs_types,nvs_api,nvs_page,nvs_pagemanager,nvs_storage,nvs_item_hash_list,nvs_handle_simple,nvs_handle_locked,nvs_partition_manager,nvs_partition,nvs_encrypted_partition,nvs_cxx_api}.cpp "$ESP_IDF"/components/nvs_flash/test_nvs_host/{esp_error_check_stub,spi_flash_emulation}.cpp "$repo/test/request_queue_nvs_test.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/"{Profile,BootstrapInput,Installer,RequestQueue}.cpp;do
 object="$build/$(basename "${file%.cpp}").o";g++ "${flags[@]}" "${includes[@]}" -c "$file" -o "$object";objects+=("$object")
done
gcc "${san[@]}" -DLINUX_TARGET -g -O1 "${includes[@]}" -c "$ESP_IDF/components/esp_rom/linux/esp_rom_crc.c" -o "$build/esp_rom_crc.o"
g++ "${san[@]}" -Wl,--wrap=nvs_open -o "$build/test" "${objects[@]}" "$build/esp_rom_crc.o" "$ESP_IDF/components/mbedtls/mbedtls/library/libmbedcrypto.a" -lcrypto
if (( $# == 0 ));then set -- '[production]';fi
"$build/test" "$@"
