#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -I"$repo/sdk/driver")
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for i in 0 1;do cc -std=c11 -shared -fPIC "${flags[@]}" "${san[@]}" -DFIXTURE_SLOT="$i" -DFIXTURE_ID="\"service-$i\"" "$repo/test/drivers/provider_service_fixture.c" -o "$build/$i.so";done
cc -std=c11 -shared -fPIC "${flags[@]}" "${san[@]}" -DSERVICE_VERSION=2 "$repo/test/drivers/provider_service_fixture.c" -o "$build/bad.so"
cc -std=c11 -shared -fPIC "${flags[@]}" "${san[@]}" -DSERVICE_TAG=123 "$repo/test/drivers/provider_service_fixture.c" -o "$build/unrelated.so"
c++ -std=c++17 "${flags[@]}" "${san[@]}" -I"$repo/sdk/hardware" -I"$repo/test/drivers/stubs" -I"$repo/src" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/test/drivers/provider_service_test.cpp" -ldl -o "$build/test"
ASAN_OPTIONS=detect_leaks=0 "$build/test" "$build/0.so" "$build/1.so" "$build/bad.so" "$build/unrelated.so"
