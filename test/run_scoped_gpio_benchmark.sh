#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
baseline="${1:-bdd389b00146f4b090acd08334b393168af49621}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir -p "$build/baseline/ports/esp32s3"
git -C "$repo" show "$baseline:src/ports/esp32s3/CpuPort.h" > "$build/baseline/ports/esp32s3/CpuPort.h"
git -C "$repo" show "$baseline:src/ports/esp32s3/CpuPort.cpp" > "$build/baseline/ports/esp32s3/CpuPort.cpp"
git -C "$repo" show "$baseline:src/ports/esp32s3/NativeHardware.cpp" > "$build/native.cpp"
python3 - "$build" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=(p/'native.cpp').read_text();a=s.index('bool stopPwm(');b=s.index('bool gpioOpen(',a)
(p/'baseline/NativePwmStop.inc').write_text('int pwmPins[4]={-1,-1,-1,-1};\nbool pwmOwnedPins[49]{};\n'+s[a:b])
PY
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -ffunction-sections -fdata-sections)
incs=(-I"$repo/src" -I"$repo/src/ports/esp32s3" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for version in baseline current; do
 extra=();source="$repo/src/ports/esp32s3/CpuPort.cpp"
 if [[ "$version" == baseline ]];then extra=(-I"$build/baseline");source="$build/baseline/ports/esp32s3/CpuPort.cpp";fi
 "${CXX:-c++}" "${flags[@]}" "${extra[@]}" "${incs[@]}" "$source" "$repo/test/scoped_gpio_benchmark.cpp" "$repo/test/scoped_gpio_benchmark_native.cpp" -Wl,--gc-sections -o "$build/$version-benchmark"
 for occupied in 0 15;do printf '%s (unrelated PWM mask %s): ' "$version" "$occupied";"$build/$version-benchmark" "$occupied";done
done
