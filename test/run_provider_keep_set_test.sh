#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source="${RUNTIME_SOURCE:-$repo}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
shared=()
if [[ "$(uname -s)" == Darwin ]]; then shared=(-Wl,-undefined,dynamic_lookup); fi
for ((i=0;i<40;i++)); do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    "${san[@]}" "${shared[@]}" -DFIXTURE_SLOT="$i" -I"$source/sdk/driver" \
    "$repo/test/drivers/provider_keep_set_fixture.c" -o "$build/provider-$i.so"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  "${san[@]}" "${shared[@]}" -DFIXTURE_SLOT=0 -DFIXTURE_CAPABILITY='"storage.files"' \
  -I"$source/sdk/driver" "$repo/test/drivers/provider_keep_set_fixture.c" -o "$build/image.so"
for profile in ${PROFILES:-cohort legacy installed expanded}; do
  options=()
  case "$profile" in
    cohort) ;;
    legacy)
      # Exercise the exact production legacy capacity branch with host dlopen.
      mkdir -p "$build/legacy/runtime"
      sed 's/^#if defined(ESP_PLATFORM).*$/#if 1/' "$source/src/runtime/RuntimeLimits.h" > "$build/legacy/runtime/RuntimeLimits.h"
      options=(-I"$build/legacy") ;;
    installed) options=(-DRISC_INSTALLED_APP_CAPACITY=64) ;;
    expanded) options=(-DRISC_INSTALLED_APP_CAPACITY=64 -DRISC_PROVIDER_CAPACITY=40) ;;
    *) printf 'Unknown profile: %s\n' "$profile" >&2; exit 1 ;;
  esac
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${san[@]}" "${options[@]}" -rdynamic \
    -I"$source/src" -I"$source/sdk/driver" -I"$source/sdk/hardware" -I"$source/test/drivers/stubs" \
    "$source/src/runtime/drivers/ProviderModuleV2.cpp" "$source/src/runtime/drivers/ProviderGraphV2.cpp" \
    "$repo/test/drivers/provider_keep_set_test.cpp" -ldl -o "$build/test-$profile"
  "$build/test-$profile" "$build"
done
