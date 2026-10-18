#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared \
  "$repo/test/fixtures/atomic_key_value_app.c" -o "$build/default.elf"
for slot in 0 1; do
  image=provider; [[ "$slot" == 0 ]] || image=unrelated
  c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DNATIVE_LIFETIME_GUARD_PROVIDER="$slot" \
    -fPIC -fvisibility=hidden -shared "$repo/test/native_serial_lifetime_guards_test.cpp" -o "$build/$image.elf"
done
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_NATIVE_SERIAL_FILES=1 -rdynamic -no-pie -pthread \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/native_serial_lifetime_guards_test.cpp" -ldl -o "$build/test"
for mode in admission default-run readiness leaked-grant leaked-consumer undrained-old-session; do
  printf '\n=== native serial lifetime guards: %s (SANITIZE=%s) ===\n' "$mode" "${SANITIZE:-0}"
  "$build/test" "$build" "$mode"
done
