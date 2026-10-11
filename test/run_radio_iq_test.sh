#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
for name in port binding; do
 "${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/radio_iq_${name}_test.cpp" -ldl -o "$build/$name"
 "$build/$name" "$build"
done
for variant in normal chip eco dram iram; do
 extra=()
 case "$variant" in
  normal) extra=(-DIQ_PORT_LIFECYCLE -DRISC_SLEEP_DIAGNOSTICS=1 "${includes[@]}" "${sources[@]}" -ldl);;
  chip) extra=(-DIQ_ROM_CHIP=5);;
  eco) extra=(-DIQ_ROM_ECO=-1);;
  dram) extra=(-DIQ_BAD_LAYOUT -Wl,--defsym,_heap_start=0x3FCB0004);;
  iram) extra=(-DIQ_BAD_LAYOUT -Wl,--defsym,_iram_end=0x403A0004);;
 esac
 "${CXX:-c++}" "${flags[@]}" "${san[@]}" -fno-pie -no-pie "${extra[@]}" -I"$repo/test/native_iq_shim" -I"$repo/sdk/driver" -I"$repo/src" "$repo/test/native_iq_test.cpp" -Wl,-T,"$repo/test/native_iq_shim/memory.ld" -o "$build/native-$variant"
 "$build/native-$variant"
done

cflags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared)
"${CC:-cc}" "${cflags[@]}" "${san[@]}" "${includes[@]}" -DIQ_PROVIDER "$repo/test/fixtures/radio_iq_lifecycle.c" -o "$build/iq.elf"
"${CC:-cc}" "${cflags[@]}" "${san[@]}" "${includes[@]}" "$repo/test/fixtures/radio_iq_lifecycle.c" -o "$build/default.elf"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/radio_iq_lifecycle_test.cpp" -ldl -o "$build/lifecycle"
"$build/lifecycle" "$build"
