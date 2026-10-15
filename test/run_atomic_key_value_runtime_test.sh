#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
if [[ "${PREFERENCE_STORE:-0}" == 1 ]];then flags+=(-DTEST_PREFERENCES);fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for child in 0 1;do
 for resident in 0 1;do
  selected=();if [[ "$resident" == 1 ]];then selected=(-DATOMIC_RESIDENT);fi
  cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -DATOMIC_APP_CHILD="$child" "${selected[@]}" "$repo/test/fixtures/atomic_key_value_app.c" -o "$build/$child-$resident.elf"
 done
done
for version in 1 2;do
 cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -DATOMIC_READER_VERSION="$version" "$repo/test/fixtures/atomic_key_value_provider.c" -o "$build/reader$version.elf"
done
extra=();if [[ -n "${SYSTEM_CONTROLLER_OBJECT:-}" ]];then extra+=("$SYSTEM_CONTROLLER_OBJECT" "${SYSTEM_HASH_OBJECT:?}");fi
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -DRISC_RUNTIME_PREFERENCE_STORE="${PREFERENCE_STORE:-0}" -DRISC_NATIVE_SETTINGS_LIFECYCLE="${NATIVE_SETTINGS_LIFECYCLE:-0}" -rdynamic -no-pie -pthread \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/atomic_key_value_runtime_test.cpp" "${extra[@]}" -ldl -o "$build/test"
for case in admission normal pending retained state-retained guard-state guard-get guard-submit guard-exit resident readthrough catalog ${SYSTEM_CONTROLLER_OBJECT:+controller};do
 resident=0;if [[ "$case" == resident ]];then resident=1;fi
 cp "$build/0-$resident.elf" "$build/default.elf";cp "$build/1-$resident.elf" "$build/child.elf"
 "$build/test" "$build" "$case"
done
