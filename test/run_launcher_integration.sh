#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
watch="$(cd "${1:?Pass the pinned Watch source checkout}" && pwd)"
system="$(cd "${2:?Pass pinned System Apps checkout}" && pwd)"
utilities="$(cd "${3:?Pass pinned Utilities checkout}" && pwd)"
[[ "$(git -C "$watch" rev-parse HEAD)" == 17ecd7c302d27b86ca68746e1b4b9495be5a467f ]]
[[ "$(git -C "$system" rev-parse HEAD)" == db2866c54b29f3f0e479f8949b8fe2abfc00fb11 ]]
[[ "$(git -C "$utilities" rev-parse HEAD)" == 3a9f3c2eb2a6fc78ba7b88b47a91e4a19128144e ]]
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cp -R "$watch/dist/launcher-deployments/sx1262-915-bma423/store/." "$build/"
# Interim deployment has duplicate package paths. Canonical runtime requires
# one artifact path with two independent instance records; do not alter source.
python3 - "$build/boot.json" <<'PYBOOT'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);b=json.loads(p.read_text())
for d in b['drivers']:
 if d['instance_id']==3:
  assert d['manifest']=='i2ctouch/manifest.json'
  d['manifest']='i2c/manifest.json'
p.write_text(json.dumps(b))
print('INTERIM STAGING: instance 3 shares i2c/manifest.json; published store requires correction')
PYBOOT
cmp "$repo/sdk/app/RiscRuntimeV1.h" "$watch/sdk/app/RiscRuntimeV1.h"
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$watch/sdk/driver" -I"$watch/include" -I"$watch" -I"$repo/test/drivers/stubs" -I"$repo/lib/ArduinoJson/src")
watchincs=(-I"$watch/sdk/app" -I"$watch/sdk/driver" -I"$watch/include" -I"$watch")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for name in gpio i2c pmu panel touch rtc; do
  src="$watch/drivers/twatch_$name/driver.c"
  if [[ "$name" == i2c ]]; then src="$watch/drivers/twatch_i2c/i2c_main.c"; fi
  cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${watchincs[@]}" "$src" -o "$build/$name/driver.elf"
done
cp "$build/i2c/driver.elf" "$build/i2ctouch/driver.elf"
portable=(-I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include")
cc -std=c11 -Wall -Wextra -Werror -DWATCH_CLOCK_LAUNCHER -fPIC -shared -fvisibility=hidden "${portable[@]}" "${watchincs[@]}" "${link[@]}" "$watch/apps/clock/main.c" "$watch/apps/clock/render.c" -o "$build/default.elf"
adapter="$system/lib/PortableApps/src/adapter.c"
battery_source="$utilities/Apps/battery.c"
if [[ "${4:-}" == --proposed ]]; then
  mkdir -p "$build/proposed/lib/PortableApps/src" "$build/proposed/Apps"
  cp "$adapter" "$build/proposed/lib/PortableApps/src/adapter.c"
  cp "$battery_source" "$build/proposed/Apps/battery.c"
  patch -s -d "$build/proposed" -p1 < "$repo/test/proposals/system-battery-unknown.patch"
  patch -s -d "$build/proposed" -p1 < "$repo/test/proposals/utilities-battery-unknown.patch"
  adapter="$build/proposed/lib/PortableApps/src/adapter.c"
  battery_source="$build/proposed/Apps/battery.c"
  incs+=(-DEXPECT_BATTERY_UI)
fi
for name in springboard battery; do
  source="$system/Apps/springboard.c"
  if [[ "$name" == battery ]]; then source="$battery_source"; fi
  cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${portable[@]}" "${watchincs[@]}" "${link[@]}" "$source" "$adapter" "$watch/dist/launcher/catalog.c" -o "$build/$name.elf"
done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
 "$repo/test/launcher_integration_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
