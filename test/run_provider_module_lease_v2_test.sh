#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
cc="${CC:-cc}"
cxx="${CXX:-c++}"
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared
       -I"$repo/sdk/hardware" -I"$repo/sdk/driver")
link=(-ldl -rdynamic)
wrap=()
if [[ "$(uname -s)" == Darwin ]]; then
  flags+=(-Wl,-undefined,dynamic_lookup)
elif [[ "$(uname -s)" == Linux ]]; then
  # Fail dlclose once without actually closing the real mapped fixture.
  wrap=(-DLEASE_WRAP_DLCLOSE -Wl,--wrap=dlclose)
fi
fixture="$repo/test/drivers/provider_module_lease_fixture.c"
"$cc" "${flags[@]}" "${san[@]}" "$fixture" -o "$build/root.so"
"$cc" "${flags[@]}" "${san[@]}" -DFIXTURE_SLOT=1 \
  -DFIXTURE_ID='"fixture-lease-child"' -DFIXTURE_CAPABILITY='"cap.lease-child"' \
  -DFIXTURE_REQUIRE='"cap.lease-root"' "$fixture" -o "$build/child.so"
"$cc" "${flags[@]}" "${san[@]}" -DFIXTURE_STREAMS "$fixture" -o "$build/streams.so"
"$cc" "${flags[@]}" "${san[@]}" -DFIXTURE_DESCRIPTOR_ABI=1 "$fixture" -o "$build/wrong-abi.so"
"$cc" "${flags[@]}" "${san[@]}" -DFIXTURE_NO_ENTRY "$fixture" -o "$build/missing-entry.so"
printf 'not an executable image\n' > "$build/corrupt.so"
"$cxx" -std=c++17 -Wall -Wextra -Werror "${san[@]}" "${wrap[@]}" \
  -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_module_lease_v2_test.cpp" \
  "${link[@]}" -o "$build/module-lease-test"
# Ptrace-based sandboxes may need ASAN_OPTIONS=detect_leaks=0 because LSan
# cannot inspect threads under ptrace; ASan and UBSan remain enabled.
ASAN_OPTIONS="detect_stack_use_after_return=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
  "$build/module-lease-test" "$build/root.so" "$build/child.so" "$build/streams.so" \
  "$build/wrong-abi.so" "$build/missing-entry.so" "$build/corrupt.so"
