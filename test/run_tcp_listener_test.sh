#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
wrap=();for call in socket setsockopt fcntl bind listen accept recv send close;do wrap+=(-Wl,--wrap="$call");done
c++ "${flags[@]}" "${san[@]}" "${incs[@]}" -I"$repo/test/native_tcp_shim" "$repo/test/native_tcp_listener_test.cpp" "${wrap[@]}" -o "$build/native"
"$build/native"
mkdir "$build/store"
for index in 0 1;do
 name=first;[[ "$index" == 1 ]] && name=second
 cc -std=c11 -Wall -Wextra -Werror "${san[@]}" "${incs[@]}" -DPROVIDER_INDEX="$index" -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/tcp_listener_provider.c" -o "$build/store/$name.elf"
done
cc -std=c11 -Wall -Wextra -Werror "${san[@]}" -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/tcp_listener_app.c" -o "$build/store/default.elf"
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
c++ "${flags[@]}" "${san[@]}" "${incs[@]}" -rdynamic "${sources[@]}" "$repo/test/tcp_listener_runtime_test.cpp" -ldl -o "$build/runtime"
"$build/runtime" "$build/store"
