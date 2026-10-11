#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
for name in port binding; do
 "${CXX:-c++}" "${flags[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/usb_phy_${name}_test.cpp" -ldl -o "$build/$name"
 "$build/$name" "$build"
done
cflags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
"${CC:-cc}" "${cflags[@]}" "${san[@]}" "${includes[@]}" -DUSB_PHY_PROVIDER "$repo/test/fixtures/usb_phy_lifecycle.c" -o "$build/usb-phy.elf"
"${CC:-cc}" "${cflags[@]}" "${san[@]}" "${includes[@]}" "$repo/test/fixtures/usb_phy_lifecycle.c" -o "$build/default.elf"
"${CXX:-c++}" "${flags[@]}" -Wno-unused-parameter -Wno-misleading-indentation \
 -DRISC_ENABLE_USB_PHY=1 -DRISC_SLEEP_DIAGNOSTICS=0 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
 -I"$repo/test/hwcdc_pinned/shim" -I"$repo/test/hwcdc_pinned/vendor" \
 -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/usb_phy_lifecycle_test.cpp" \
 "$repo/test/hwcdc_pinned/usb_phy_lifecycle_bridge.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -ldl -o "$build/lifecycle"
"$build/lifecycle" "$build"
for stage in 0 1; do
 for diagnostics in 0 1; do
  "${CXX:-c++}" "${flags[@]}" -Wno-unused-parameter -Wno-misleading-indentation \
   -DRISC_ENABLE_USB_PHY=1 -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1 -DRISC_STAGE_LOGS=$stage -DRISC_SLEEP_DIAGNOSTICS=$diagnostics \
   -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
   -I"$repo/test/hwcdc_pinned/shim" -I"$repo/test/hwcdc_pinned/vendor" -I"$repo/src" -I"$repo/sdk/driver" \
   "$repo/test/hwcdc_pinned/usb_phy_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/native"
  "$build/native"
 done
done
