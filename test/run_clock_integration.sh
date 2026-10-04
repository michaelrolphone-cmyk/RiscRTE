#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
watch="$(cd "${1:?Pass the pinned Watch source checkout}" && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
python3 "$repo/test/stage_clock_test.py" "$watch" "$build"
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$watch/sdk/driver" -I"$watch/include" -I"$watch" -I"$repo/test/drivers/stubs" -I"$repo/lib/ArduinoJson/src")
watchincs=(-I"$watch/sdk/app" -I"$watch/sdk/driver" -I"$watch/include" -I"$watch")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for name in gpio i2c pmu panel rtc; do
  src="$watch/drivers/twatch_$name/driver.c"
  if [[ "$name" == i2c ]]; then src="$watch/drivers/twatch_i2c/i2c_main.c"; fi
  cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${watchincs[@]}" "$src" -o "$build/$name/driver.elf"
done
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${watchincs[@]}" "${link[@]}" "$watch/apps/clock/main.c" "$watch/apps/clock/render.c" -o "$build/default.elf"
cc -std=c11 -Wall -Wextra -Werror "${watchincs[@]}" "$watch/apps/clock/render.c" "$watch/tests/clock_frame_fixture.c" -o "$build/golden"
"$build/golden" valid > "$build/valid.rgb565"
"$build/golden" unset > "$build/unset.rgb565"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
 "$repo/test/clock_integration_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
