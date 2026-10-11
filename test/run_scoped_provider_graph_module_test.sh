#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
python3 "$repo/test/make_scoped_provider_elf.py" "$build"
common=(-D_GNU_SOURCE -Wall -Wextra -Werror -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1
  -I"$repo/test/scoped_provider_stubs" -I"$repo/test/image_pressure_stubs"
  -I"$repo/test/support/native_registry/stubs"
  -I"$repo/test/drivers/stubs" -I"$repo/lib/elf_loader/include" -I"$repo/src"
  -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
if [[ "${SANITIZE:-0}" == 1 ]];then common+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
for source in esp_elf_validate esp_privileged_imports esp_privileged_manifest_imports;do
  cc -std=c11 "${common[@]}" -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$source.o"
done
c++ -std=c++17 -DESP_PLATFORM=1 "${common[@]}" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/scoped_provider_graph_module_test.cpp" \
  "$build"/*.o -lcrypto -ldl -o "$build/test"
"$build/test" "$build/selected.elf"
