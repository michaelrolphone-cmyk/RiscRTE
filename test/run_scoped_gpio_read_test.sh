#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${GPIO_READ_BUILD:-$(mktemp -d)}";mkdir -p "$build"
if [[ -z "${GPIO_READ_BUILD:-}" ]];then trap 'rm -rf "$build"' EXIT;fi
flags=(-std=c++17 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -ffunction-sections -fdata-sections)
incs=(-I"$repo/src" -I"$repo/src/ports/esp32s3" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -fno-pie -no-pie);fi
for fixture in scoped_gpio_read_test scoped_gpio_write_test;do
 "${CXX:-c++}" "${flags[@]}" "${san[@]}" "${incs[@]}" "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/$fixture.cpp" -Wl,--gc-sections -o "$build/$fixture"
 ASAN_OPTIONS=detect_leaks=0 "$build/$fixture"
done
if [[ "${SANITIZE:-0}" == 1 ]];then exit;fi
# Original and canonical public .81 pins have exactly this tree. The public
# commit is used by CI; local original-history checkouts need no network fetch.
base="${GPIO_READ_BASE:-faa8f62936a5889c65cce3f271758d9d95f21d7b}"
if ! git -C "$repo" cat-file -e "$base^{commit}" 2>/dev/null;then
 base=aaa77ad11fc66b42430254ab7065590d33fd1fca
fi
[[ "$(git -C "$repo" rev-parse "$base^{tree}")" == 54c364ab87f6b465ce1ea0a43c0aae82e82ce0f5 ]]
mkdir -p "$build/baseline/ports/esp32s3"
for file in CpuPort.cpp CpuPort.h;do git -C "$repo" show "$base:src/ports/esp32s3/$file" > "$build/baseline/ports/esp32s3/$file";done
for variant in baseline current;do
 extra=();source="$repo/src/ports/esp32s3/CpuPort.cpp"
 # The instrumented copy lives outside the original source directory. Both
 # qualified and unqualified header includes must resolve to its own layout.
 if [[ "$variant" == baseline ]];then extra=(-I"$build/baseline" -I"$build/baseline/ports/esp32s3");source="$build/baseline/ports/esp32s3/CpuPort.cpp";fi
 "${CXX:-c++}" "${flags[@]}" "${extra[@]}" "${incs[@]}" "$source" "$repo/test/scoped_gpio_read_benchmark.cpp" -Wl,--gc-sections -o "$build/$variant-benchmark"
 for mode in payload cold mixed collision;do printf '%s: ' "$variant";"$build/$variant-benchmark" "$mode";done
 # A separate, untimed instrumented copy counts executed search predicates;
 # timing above always uses the unmodified production translation unit.
 python3 - "$source" "$build/$variant-count.cpp" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text();a=s.index('bool Port::gpioRead(');b=s.index('bool Port::gpioPwm(',a)
fragment=s[a:b];needle='p.pins_[i].owner==&c && p.pins_[i].token==token'
assert fragment.count(needle)==1
fragment=fragment.replace(needle,'gpio_read_probe('+needle+')')
Path(sys.argv[2]).write_text('extern "C" bool gpio_read_probe(bool);\n'+s[:a]+fragment+s[b:])
PY
 "${CXX:-c++}" "${flags[@]}" -DGPIO_READ_COUNT "${extra[@]}" "${incs[@]}" "$build/$variant-count.cpp" "$repo/test/scoped_gpio_read_benchmark.cpp" -Wl,--gc-sections -o "$build/$variant-count"
 for mode in payload cold mixed collision;do printf '%s counted: ' "$variant";"$build/$variant-count" "$mode";done
done
