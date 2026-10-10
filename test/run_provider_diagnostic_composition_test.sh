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
(build/'diagnostics.elf').write_bytes(image(['memcpy','printf','putchar','puts','xTaskGetTickCount']))
names=re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)',(repo/'lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def').read_text(),re.M)
real={'abort','__stack_chk_fail','xTaskGetCurrentTaskHandle','xTaskGetTickCount','vTaskDelay','heap_caps_malloc','heap_caps_free'}
(build/'inventory.c').write_text(''.join('const unsigned char test_native_'+n+'[] __asm__("'+n+'") = {0};\n' for n in names if n not in real))
PY
common=(-D_GNU_SOURCE -O1 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1 -DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1 -DCONFIG_ELF_LOADER_LOAD_PSRAM=1
  -DRISC_STAGE_LOGS=0 -DRISC_SLEEP_DIAGNOSTICS=0 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1
  -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0
  -I"$repo/test/stage_shim" -I"$repo/test/diagnostic_shim"
  -I"$repo/test/scoped_provider_stubs" -I"$repo/test/performance_loader_stubs"
  -I"$repo/test/image_pressure_stubs" -I"$repo/test/support/native_registry/stubs"
  -I"$repo/test/drivers/stubs" -I"$repo/lib/elf_loader/include" -I"$repo/src"
  -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
if [[ "${SANITIZE:-0}" == 1 ]];then common+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
for source in esp_elf esp_elf_symbol esp_elf_validate esp_privileged_elf esp_privileged_os_cpu esp_privileged_imports esp_privileged_manifest_imports;do
 cc -std=c11 -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast "${common[@]}" -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$source.o"
done
cc -std=c11 "${common[@]}" -c "$build/inventory.c" -o "$build/inventory.o"
cc -std=c11 "${common[@]}" -c "$repo/test/provider_diagnostic_loader_stubs.c" -o "$build/stubs.o"
policy=()
if [[ -n "${CONTROLLER_POLICY_HEADER:-}" ]];then policy=(-DTEST_CONTROLLER_POLICY_HEADER=\"$CONTROLLER_POLICY_HEADER\");fi
c++ -std=c++17 -DESP_PLATFORM=1 "${policy[@]}" "${common[@]}" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/ProviderDiagnostics.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" \
 "$repo/test/provider_diagnostic_composition_test.cpp" "$build"/*.o -lcrypto -ldl -o "$build/test"
"$build/test" "$build/diagnostics.elf" ${CONTROLLER_ELF:+"$CONTROLLER_ELF"}
