#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
loading=();if [[ "${RESIDENT_LOADING:-0}" == 1 ]];then loading=(-DRESIDENT_LOADING_TEST);flags+=("${loading[@]}");fi
link=(-g);if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for role in 0 1 2;do
  cc "${flags[@]}" "${link[@]}" -DRESIDENT_APP_ROLE="$role" "$repo/test/fixtures/resident_app.c" -o "$build/role-$role.elf"
done
cc "${flags[@]}" "${link[@]}" -DRESIDENT_APP_ROLE=3 -DRESIDENT_NO_DESCRIPTOR "$repo/test/fixtures/resident_app.c" -o "$build/bad.elf"
cc "${flags[@]}" "${link[@]}" -I"$repo/sdk/driver" "$repo/test/fixtures/default_request_provider.c" -o "$build/provider.elf"
for provider in stream_session_provider stream_session_root;do
 cc "${flags[@]}" "${link[@]}" -I"$repo/sdk/driver" "$repo/test/fixtures/$provider.c" -o "$build/$provider.elf"
done
cp "$build/stream_session_provider.elf" "$build/stream.elf"
cp "$build/stream_session_root.elf" "$build/root.elf"
native=()
extra=()
if [[ "${RESIDENT_NATIVE_MEMORY:-0}" == 1 ]];then
 native=(-DRISC_NATIVE_APP_MEMORY_TEST -I"$repo/test/resident_memory_stubs" -I"$repo/test/image_pressure_stubs")
 c++ "${san[@]}" "${native[@]}" -std=c++17 -Wall -Wextra -Werror -pthread -I"$repo/src" \
   -include "$repo/test/image_pressure_stubs/alloc_redirect.h" -c "$repo/src/native/NativeAppMemory.cpp" -o "$build/memory.o"
 extra=("$build/memory.o" -pthread)
fi
c++ "${san[@]}" "${native[@]}" -DRISC_METADATA_ALLOCATION_TEST -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "${loading[@]}" "$repo/test/resident_shell_test.cpp" "${extra[@]}" -ldl -o "$build/test"
modes="${RESIDENT_SCENARIOS:-normal wake-host streams stream-retained busy exit home file-open descriptor init-failure load-failure context-oom inventory-oom names-oom child-retained callback-retained fini-retained native-retained invalid-callback provider-retained release-retained policy-host policy-duplicate policy-missing policy-null policy-wake-child failure-callback-retained prelaunch-retained register-retained}"
if [[ "${RESIDENT_NATIVE_MEMORY:-0}" == 1 && -z "${RESIDENT_SCENARIOS:-}" ]];then
 modes+=" memory-lock-retained switch-lock-retained memory-oom"
fi
for mode in $modes;do
  cp "$build/role-0.elf" "$build/host.elf"
  cp "$build/role-1.elf" "$build/child.elf"
  cp "$build/role-2.elf" "$build/next.elf"
  "$build/test" "$build" "$mode"
done
