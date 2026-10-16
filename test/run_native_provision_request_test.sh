#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for native in 0 1;do
c++ "${san[@]}" -DRISC_NATIVE_ESP_IDF="$native" -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -DRISC_PAIRED_BANKS=1 -DRISC_NATIVE_PROVISION_REQUEST=1 \
 -I"$repo/test/native_nvs_shim" -I"$repo/test/native_bank_stubs" -I"$repo/test/drivers/stubs" -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/sdk/app" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" "$repo/src/runtime/provisioning/Installer.cpp" "$repo/src/runtime/provisioning/RequestQueue.cpp" "$repo/test/native_provision_request_test.cpp" -lcrypto -o "$build/test"
"$build/test"
done
