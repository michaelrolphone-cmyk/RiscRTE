#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
python3 - "$repo" "$build" <<'PY'
import pathlib,re,sys
repo,build=map(pathlib.Path,sys.argv[1:])
sys.path.insert(0,str(repo/'test'))
from make_scoped_provider_elf import image
(build/'diagnostics.elf').write_bytes(image(['printf','putchar','puts']))
(build/'ordinary.elf').write_bytes(image(['memcpy']))
names=re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)',(repo/'lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def').read_text(),re.M)
real={'abort','__stack_chk_fail','xTaskGetCurrentTaskHandle','xTaskGetTickCount','vTaskDelay'}
(build/'inventory.c').write_text(''.join('const unsigned char test_native_'+n+'[] __asm__("'+n+'") = {0};\n' for n in names if n not in real))
PY
flags=(-std=c11 -D_GNU_SOURCE -O1 -Wall -Wextra -Werror
  -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1 -DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1
  -DCONFIG_ELF_LOADER_LOAD_PSRAM=1
  -I"$repo/test/scoped_provider_stubs" -I"$repo/test/performance_loader_stubs"
  -I"$repo/test/image_pressure_stubs" -I"$repo/test/support/native_registry/stubs"
  -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]];then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g)
fi
for source in esp_elf esp_elf_symbol esp_elf_validate esp_privileged_elf esp_privileged_os_cpu esp_privileged_imports esp_privileged_manifest_imports;do
  "${CC:-cc}" "${flags[@]}" -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$source.o"
done
"${CC:-cc}" "${flags[@]}" -c "$build/inventory.c" -o "$build/inventory.o"
"${CC:-cc}" "${flags[@]}" -c "$repo/test/provider_diagnostic_loader_stubs.c" -o "$build/stubs.o"
for profile in enabled disabled marker_zero query_zero marker_unknown query_unknown missing_marker missing_query missing_printf missing_puts missing_putchar;do
  variant=()
  case "$profile" in
    enabled) variant=(-DTEST_AVAILABLE=1);;
    disabled) variant=(-DTEST_MARKER_ABI=0 -DTEST_QUERY_ABI=0);;
    marker_zero) variant=(-DTEST_MARKER_ABI=0);;
    query_zero) variant=(-DTEST_QUERY_ABI=0);;
    marker_unknown) variant=(-DTEST_MARKER_ABI=2);;
    query_unknown) variant=(-DTEST_QUERY_ABI=2);;
    missing_marker) variant=(-DTEST_MISSING_MARKER);;
    missing_query) variant=(-DTEST_MISSING_QUERY);;
    missing_printf) variant=(-DTEST_MISSING_PRINTF);;
    missing_puts) variant=(-DTEST_MISSING_PUTS);;
    missing_putchar) variant=(-DTEST_MISSING_PUTCHAR);;
  esac
  "${CC:-cc}" "${flags[@]}" "${variant[@]}" \
    "$repo/test/provider_diagnostic_availability_test.c" "$build"/*.o -o "$build/test"
  "$build/test" "$build/diagnostics.elf" "$build/ordinary.elf"
  echo "provider diagnostic real-loader availability=$profile PASS"
done
