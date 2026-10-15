#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/atomic_key_value_app.c" -o "$build/default.elf"
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -DATOMIC_READER_VERSION=1 "$repo/test/fixtures/atomic_key_value_provider.c" -o "$build/reader.elf"
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_RUNTIME_PREFERENCE_STORE=1 -rdynamic -no-pie -pthread \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/preference_store_runtime_test.cpp" -ldl -o "$build/test"
for case in admission normal copy partial failed-first busy-first bad-read uncollected;do "$build/test" "$build" "$case";done
PREFERENCE_STORE=1 bash "$repo/test/run_atomic_key_value_runtime_test.sh"
