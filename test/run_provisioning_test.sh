#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" "$repo/test/fixtures/default.c" -o "$build/default.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/Coordinator.cpp" \
 "$repo/test/provisioning_test.cpp" -lcrypto -ldl -o "$build/test"
"$build/test" "$build"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/StoreFiles.cpp" \
 "$repo/test/provisioning_files_test.cpp" -lcrypto -ldl -o "$build/files-test"
"$build/files-test" "$build"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
 -I"$repo/test/native_nvs_shim" -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" \
 "$repo/test/bootstrap_input_test.cpp" -lcrypto -o "$build/input-test"
"$build/input-test"
bash "$repo/scripts/build_provision_input_tool.sh" "$build/provision-input"
python3 "$repo/test/provision_input_tool_test.py" "$build/provision-input"
bash "$repo/test/run_native_sntp_test.sh"
c++ "${san[@]}" -DRISC_PAIRED_BANKS=1 -std=c++17 -Wall -Wextra -Werror \
 -I"$repo/test/native_nvs_shim" -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" \
 "$repo/src/runtime/provisioning/Installer.cpp" "$repo/test/owner_nvs_install_test.cpp" -o "$build/install-test"
"$build/install-test"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" \
 "$repo/src/runtime/provisioning/Installer.cpp" "$repo/src/runtime/provisioning/Maintenance.cpp" \
 "$repo/test/maintenance_test.cpp" -lcrypto -o "$build/maintenance-test"
"$build/maintenance-test"
python3 "$repo/test/provision_serial_test.py" "$build/maintenance-test" "$build/provision-input"
