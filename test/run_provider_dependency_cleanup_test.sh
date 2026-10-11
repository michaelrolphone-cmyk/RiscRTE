#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
shared=()
if [[ "$(uname -s)" == Darwin ]]; then shared=(-Wl,-undefined,dynamic_lookup); fi
for i in 0 1 2 3; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    "${san[@]}" "${shared[@]}" -DFIXTURE_SLOT="$i" -I"$repo/sdk/driver" \
    "$repo/test/drivers/provider_dependency_cleanup_fixture.c" -o "$build/provider-$i.so"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${san[@]}" -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/test/drivers/provider_dependency_cleanup_test.cpp" \
  -ldl -o "$build/test"
"$build/test" "$build"/provider-{0,1,2,3}.so
