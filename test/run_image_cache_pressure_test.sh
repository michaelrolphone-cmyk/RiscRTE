#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
stubs="$repo/test/image_pressure_stubs"
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]];then san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
shared=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver")
if [[ "$(uname -s)" == Darwin ]];then shared+=(-Wl,-undefined,dynamic_lookup);fi
cc "${shared[@]}" "$repo/test/fixtures/image_pressure_app.c" -o "$build/default.elf"
cc "${shared[@]}" "$repo/test/fixtures/image_pressure_provider.c" -o "$build/provider.elf"
python3 - "$build/default.elf" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]);data=p.read_bytes();assert len(data)<512*1024;p.write_bytes(data+bytes(512*1024-len(data)))
PY
for cache in 0 1;do
  out="$build/$cache";mkdir -p "$out"
  common=("${san[@]}" -D_GNU_SOURCE -DRISC_APP_IMAGE_CACHE="$cache" -DRISC_PAIRED_BANKS=1 -DRISC_PERFORMANCE_TRACE=1
    -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -pthread
    -DCONFIG_ELF_LOADER_LOAD_PSRAM=1 -DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1
    -I"$stubs" -I"$repo/test/support/native_registry/stubs" -I"$repo/lib/elf_loader/include"
    -I"$repo/src" -I"$repo/test" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src")
  for src in dlso/dlfcn dlso/dlmod esp_elf_adapter esp_elf_symbol;do
    cc -std=c11 "${common[@]}" -include "$repo/test/support/native_registry/redirect.h" \
      -include "$stubs/alloc_redirect.h" -c "$repo/lib/elf_loader/src/$src.c" -o "$out/$(basename "$src").o"
  done
  cc -std=c11 "${common[@]}" -include "$stubs/alloc_redirect.h" \
    -Desp_elf_malloc=unused_host_elf_malloc -Desp_elf_free=unused_host_elf_free \
    -c "$repo/test/support/native_registry/backend.c" -o "$out/backend.o"
  for src in bootstrap/Json bootstrap/Board bootstrap/Runtime runtime/drivers/ProviderGraphV2 runtime/drivers/ProviderModuleV2 native/NativeAppMemory;do
    extra=();if [[ "$src" == bootstrap/Runtime || "$src" == native/NativeAppMemory ]];then extra=(-DESP_PLATFORM=1);fi
    c++ -std=c++17 "${common[@]}" "${extra[@]}" -include "$repo/test/support/native_registry/redirect.h" \
      -include "$stubs/alloc_redirect.h" -c "$repo/src/$src.cpp" -o "$out/$(basename "$src").o"
  done
  c++ -std=c++17 "${common[@]}" -rdynamic -include "$repo/test/support/native_registry/redirect.h" \
    "$repo/test/image_cache_pressure_test.cpp" "$out"/*.o -ldl -o "$out/test"
  for mode in provider-mapping provider-start provider-retained malloc calloc realloc caps failed-retry foreign app-retained cache-hit ledger-init;do
    "$out/test" "$build" "$mode"
  done
done
