#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/atomic_key_value_app.c" -o "$build/default.elf"
for index in 0 1;do cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -DFILE_PROVIDER_INDEX="$index" "$repo/test/fixtures/storage_files_runtime_provider.c" -o "$build/provider$index.elf";done
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_RUNTIME_STORAGE_FILES=1 -DRISC_RUNTIME_PREFERENCE_STORE="${PREFERENCE_STORE:-0}" -DRISC_RUNTIME_ATOMIC_KEY_VALUE="${ATOMIC_KEY_VALUE:-0}" -rdynamic -no-pie -pthread \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/storage_files_runtime_test.cpp" -ldl -o "$build/test"
for case in normal unique direct reentrant malformed-direct unavailable open-exit failed-close policies direct-policies;do "$build/test" "$build" "$case";done
