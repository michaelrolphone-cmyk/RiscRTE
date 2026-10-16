#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]];then
  san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
shared=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver")
if [[ "$(uname -s)" == Darwin ]];then shared+=(-Wl,-undefined,dynamic_lookup);fi
cc "${shared[@]}" "$repo/test/fixtures/runtime_image_custody.c" -o "$build/root.elf"
cc "${shared[@]}" -DCUSTODY_CHILD "$repo/test/fixtures/runtime_image_custody.c" -o "$build/child.elf"
cc "${shared[@]}" -DCUSTODY_APP "$repo/test/fixtures/runtime_image_custody.c" -o "$build/default.elf"
python3 - "$build/child.elf" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); data=p.read_bytes(); assert len(data)<256*1024
p.write_bytes(data+bytes(256*1024-len(data)))
PY
cflags=("${san[@]}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-sign-compare -include sys/types.h -pthread
  -I"$repo/test/support/native_registry/stubs" -I"$repo/lib/elf_loader/include")
for source in dlfcn dlmod;do
  extra=();if [[ "$source" == dlfcn ]];then extra=(-Desp_dlopen_reader_instance=risc_test_real_dlopen_reader_instance);fi
  cc "${cflags[@]}" "${extra[@]}" -include "$repo/test/support/native_registry/redirect.h" \
    -c "$repo/lib/elf_loader/src/dlso/$source.c" -o "$build/$source.o"
done
# Replace only the backend allocator entry points to inject the exact image
# allocation failure. Production Runtime, dlfcn, GraphV2 and leases are real.
cc "${cflags[@]}" -Desp_elf_malloc=risc_test_base_elf_malloc -Desp_elf_free=risc_test_base_elf_free \
  -Desp_elf_close=risc_test_base_elf_close \
  -c "$repo/test/support/native_registry/backend.c" -o "$build/backend.o"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -pthread \
  -DRISC_APP_IMAGE_CACHE_TEST=1 -DRISC_APP_IMAGE_CACHE=1 -DRISC_PERFORMANCE_TRACE=1 -include "$repo/test/support/native_registry/redirect.h" \
  -I"$repo/src" -I"$repo/test" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/support/native_registry/stubs" -I"$repo/lib/elf_loader/include" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/runtime_image_custody_test.cpp" "$build"/*.o -ldl -o "$build/test"
for mode in clean read-retained close-failed oom-close-failed fairness-0ms fairness-1ms fairness-3ms read-timeout yield-timeout oom-clean;do
  if [[ "${SANITIZE:-0}" == 1 && ( "$mode" == read-retained || "$mode" == close-failed || "$mode" == oom-close-failed ) ]];then
    # Only terminal owners intentionally survive to process exit. Clean and
    # recoverable cases keep the caller's normal leak-check configuration.
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" "$build/test" "$build" "$mode"
  else
    "$build/test" "$build" "$mode"
  fi
done
