#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-DRISC_NATIVE_STORAGE_CUSTODY=1 -DRISC_METADATA_ALLOCATION_TEST -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${NATIVE_SETTINGS_CHECKPOINT:-0}" == 1 ]];then
  flags+=(-DRISC_NATIVE_SETTINGS_LIFECYCLE=1)
  printf '%s\n' '__attribute__((visibility("default"))) void app_main(void){}' > "$build/app.c"
  "${CC:-cc}" -fPIC -fvisibility=hidden -shared "$build/app.c" -o "$build/default.elf"
fi
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize="${SANITIZERS:-undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
includes=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for name in selected foreign dependency; do
  extra=(-DPIN_ID="\"$name-storage\"")
  if [[ "$name" == dependency ]]; then extra=(-DPIN_DEPENDENCY -DPIN_ID='"pin-dependency"');fi
  "${CC:-cc}" -std=c11 "${flags[@]}" "${san[@]}" "${includes[@]}" -fPIC -fvisibility=hidden -shared "${extra[@]}" \
    "$repo/test/fixtures/native_runtime_storage_pin.c" -o "$build/$name.elf"
done
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp")
pie=()
if [[ "${NO_PIE:-0}" == 1 || "${SANITIZERS:-}" == *address* ]]; then pie=(-fno-pie -no-pie);fi
"${CXX:-c++}" -std=c++17 "${flags[@]}" "${san[@]}" "${pie[@]}" "${includes[@]}" -rdynamic \
  "${sources[@]}" "$repo/test/native_runtime_storage_pin_test.cpp" -ldl -o "$build/test"
for case in ${NATIVE_PIN_CASES:-happy unprepared invalid-board invalid-graph allocation-failed no-installation wrong-installation wrong-device zero-device wrong-port wrong-runtime wrong-owner missing-image wrong-image start-failed invalid-suffix grant-capacity multiple-grants fresh-generation reentry release-retained dependency-retained start-retained invalid-suffix-retained destructor-held destructor-retained destructor-acquiring destructor-releasing destructor-runtime}; do
  "$build/test" "$build" "$case"
done
