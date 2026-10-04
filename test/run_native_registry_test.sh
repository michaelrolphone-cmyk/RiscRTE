#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
support="$repo/test/support/native_registry"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc="${CC:-cc}"; cxx="${CXX:-c++}"
mkdir -p "$build/hardware" "$build/service" "$build/other" "$build/corrupt"
shared=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
if [[ "$(uname -s)" == Darwin ]]; then shared+=(-Wl,-undefined,dynamic_lookup); fi
"$cc" "${shared[@]}" "${flags[@]}" "$repo/test/drivers/native_registry_fixture.c" -o "$build/hardware/driver.elf"
"$cc" "${shared[@]}" "${flags[@]}" -DFIXTURE_SLOT=1 -DFIXTURE_ID='"registry-service"' -DFIXTURE_CAP='"test.service"' "$repo/test/drivers/native_registry_fixture.c" -o "$build/service/driver.elf"
"$cc" "${shared[@]}" "${flags[@]}" -DFIXTURE_SLOT=2 -DFIXTURE_ID='"registry-other"' -DFIXTURE_CAP='"test.other"' "$repo/test/drivers/native_registry_fixture.c" -o "$build/other/driver.elf"
printf 'not an ELF\n' > "$build/corrupt/driver.elf"
bash "$support/build.sh" "$build"
"$cxx" -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -pthread -rdynamic \
  -include "$support/redirect.h" -I"$support/stubs" -I"$repo/lib/elf_loader/include" \
  -I"$repo/src" -I"$repo/test" -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/native_registry_test.cpp" "$build/target-dlfcn.o" "$build/target-dlmod.o" "$build/host-elf-backend.o" \
  -ldl -o "$build/native-registry-test"
"$build/native-registry-test" "$build/hardware/driver.elf" "$build/service/driver.elf" "$build/other/driver.elf" "$build/corrupt/driver.elf"
