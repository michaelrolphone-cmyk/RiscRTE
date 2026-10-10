#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
loading=();if [[ "${RESIDENT_LOADING:-0}" == 1 ]];then loading=(-DRESIDENT_LOADING_TEST);flags+=("${loading[@]}");fi
link=(-g)
if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for role in 0 1 2 3 4 5;do
  cc "${flags[@]}" "${link[@]}" -DLEGACY_TEST_ROLE="$role" "$repo/test/fixtures/resident_legacy_app.c" -o "$build/role-$role.elf"
done
for provider in default_request_provider stream_session_provider stream_session_root;do
  cc "${flags[@]}" "${link[@]}" -I"$repo/sdk/driver" "$repo/test/fixtures/$provider.c" -o "$build/$provider.elf"
done
native=();extra=()
if [[ "${RESIDENT_NATIVE_MEMORY:-0}" == 1 ]];then
  native=(-DRISC_NATIVE_APP_MEMORY_TEST -I"$repo/test/resident_memory_stubs" -I"$repo/test/image_pressure_stubs")
  c++ "${san[@]}" "${native[@]}" -std=c++17 -Wall -Wextra -Werror -pthread -I"$repo/src" \
    -include "$repo/test/image_pressure_stubs/alloc_redirect.h" -c "$repo/src/native/NativeAppMemory.cpp" -o "$build/memory.o"
  extra=("$build/memory.o" -pthread)
fi
c++ "${san[@]}" "${native[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "${loading[@]}" "$repo/test/resident_legacy_test.cpp" "${extra[@]}" -ldl -o "$build/test"
modes="${RESIDENT_LEGACY_SCENARIOS:-direct repeated-chain unconsumed-continuation legacy-home legacy-launch-default legacy-launch-legacy child-launch-legacy legacy-launch-resident denied legacy-empty legacy-absent host-init-failure child-init-failure child-load-failure legacy-init-failure legacy-load-failure reload-host-init-failure reload-host-load-failure file-resident-legacy file-resident-legacy-home file-resident-legacy-init-failure file-resident-legacy-load-failure file-legacy-resident file-legacy-resident-home file-legacy-resident-init-failure file-legacy-resident-load-failure host-init-retained host-entry-retained host-fini-retained host-grant-retained host-stream-retained host-unload-retained child-init-retained child-entry-retained child-fini-retained child-grant-retained child-stream-retained child-unload-retained legacy-init-retained legacy-entry-retained legacy-fini-retained legacy-grant-retained legacy-stream-retained legacy-unload-retained legacy-explicit-retained file-resident-legacy-home-retained file-legacy-resident-home-retained policy-legacy-null policy-legacy-string policy-legacy-object policy-legacy-number policy-legacy-bool policy-legacy-item-null policy-legacy-item-number policy-legacy-duplicate policy-legacy-host policy-legacy-foreground policy-legacy-unadmitted policy-legacy-traversal policy-legacy-absolute policy-unknown}"
if [[ "${RESIDENT_NATIVE_MEMORY:-0}" == 1 && -z "${RESIDENT_LEGACY_SCENARIOS:-}" ]];then
  modes+=" host-memory-retained child-memory-retained legacy-memory-retained"
fi
for mode in $modes;do
  cp "$build/role-0.elf" "$build/host.elf"
  cp "$build/role-1.elf" "$build/child.elf"
  cp "$build/role-2.elf" "$build/legacy.elf"
  cp "$build/role-3.elf" "$build/receiver.elf"
  cp "$build/role-4.elf" "$build/unlisted.elf"
  cp "$build/role-5.elf" "$build/legacy-next.elf"
  "$build/test" "$build" "$mode"
done
