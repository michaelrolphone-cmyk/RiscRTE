#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
if [[ "$(uname -s)" == Darwin ]]; then flags+=(-Wl,-undefined,dynamic_lookup); fi
cc "${flags[@]}" "$repo/test/fixtures/image_cache_app.c" -o "$build/default.elf"
cc "${flags[@]}" -DIMAGE_CACHE_CHILD "$repo/test/fixtures/image_cache_app.c" -o "$build/child.elf"
cc "${flags[@]}" -DIMAGE_CACHE_CHILD -DIMAGE_REVISION=2 "$repo/test/fixtures/image_cache_app.c" -o "$build/replacement.elf"
cp "$build/child.elf" "$build/loose.elf"
bash "$repo/test/support/native_registry/build.sh" "$build"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -pthread \
  -DRISC_APP_IMAGE_CACHE_TEST=1 -include "$repo/test/support/native_registry/redirect.h" \
  -I"$repo/src" -I"$repo/test" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/support/native_registry/stubs" -I"$repo/lib/elf_loader/include" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/image_cache_runtime_test.cpp" "$build/target-dlfcn.o" "$build/target-dlmod.o" "$build/host-elf-backend.o" \
  -ldl -o "$build/test"
for mode in admitted loose missing init-failure retained replacement; do "$build/test" "$build" "$mode"; done
