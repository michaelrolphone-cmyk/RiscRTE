#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
watch="$(cd "${1:?Pass the pinned Watch source checkout}" && pwd)"
system="$(cd "${2:?Pass pinned System Apps checkout}" && pwd)"
utilities="$(cd "${3:?Pass pinned Utilities checkout}" && pwd)"
[[ "$(git -C "$watch" rev-parse HEAD)" == ee8dd8b9804fa183141320998ce9f14db0ee2141 ]]
[[ "$(git -C "$system" rev-parse HEAD)" == 2df9ba0e44c4cebc1b461b513ae9a63779a8ac77 ]]
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
for name in springboard battery; do
  source="$system/Apps/springboard.c"
  if [[ "$name" == battery ]]; then source="$utilities/Apps/battery.c"; fi
  cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${portable[@]}" "${watchincs[@]}" "${link[@]}" "$source" "$system/lib/PortableApps/src/adapter.c" "$watch/dist/launcher/catalog.c" -o "$build/$name.elf"
done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
 "$repo/test/launcher_integration_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
