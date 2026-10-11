#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=${SANITIZERS:-address,undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
shared_flags=();host_flags=(-rdynamic)
if [[ "$(uname -s)" == Darwin ]];then shared_flags=(-Wl,-undefined,dynamic_lookup);host_flags=(-Wno-unused-private-field -Wno-unused-but-set-variable);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for i in 0 1; do
 cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${shared_flags[@]}" "${incs[@]}" -DPROVIDER_INDEX="$i" "$repo/test/fixtures/bound_app_data_provider.c" -o "$build/provider$i.elf"
done
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${shared_flags[@]}" "${incs[@]}" "$repo/test/fixtures/bound_app_data_relay.c" -o "$build/relay.elf"
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${shared_flags[@]}" "$repo/test/fixtures/bound_app_data_app.c" -o "$build/default.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${host_flags[@]}" "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/bound_app_data_runtime_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
