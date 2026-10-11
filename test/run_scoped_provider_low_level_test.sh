#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
python3 "$repo/test/make_scoped_provider_elf.py" "$build"
# The inventory's code addresses are linked, not executed. Real test RTOS hooks
# and libc symbols retain their native host definitions.
python3 - "$repo/lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def" "$build/inventory.c" <<'PY'
import pathlib,re,sys
names=re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)',pathlib.Path(sys.argv[1]).read_text(),re.M)
real={'abort','__stack_chk_fail','xTaskGetCurrentTaskHandle','xTaskGetTickCount','vTaskDelay'}
pathlib.Path(sys.argv[2]).write_text(''.join('const unsigned char test_native_'+n+'[] __asm__("'+n+'") = {0};\n' for n in names if n not in real))
PY
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-pointer-to-int-cast
  -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1 -DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1
  -DCONFIG_ELF_LOADER_LOAD_PSRAM=1
  -I"$repo/test/scoped_provider_stubs" -I"$repo/test/performance_loader_stubs"
  -I"$repo/test/image_pressure_stubs" -I"$repo/test/support/native_registry/stubs"
  -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cc "${flags[@]}" "$repo/test/scoped_provider_low_level_test.c" "$build/inventory.c" \
  "$repo/lib/elf_loader/src/esp_elf.c" "$repo/lib/elf_loader/src/esp_elf_symbol.c" \
  "$repo/lib/elf_loader/src/esp_elf_validate.c" "$repo/lib/elf_loader/src/esp_privileged_elf.c" \
  "$repo/lib/elf_loader/src/esp_privileged_os_cpu.c" "$repo/lib/elf_loader/src/esp_privileged_imports.c" \
  "$repo/lib/elf_loader/src/esp_privileged_manifest_imports.c" -o "$build/test"
"$build/test" "$build"
