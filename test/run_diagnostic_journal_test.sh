#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" "$repo/test/diagnostic_journal_test.cpp" -o "$build/test"
"$build/test"
"${CXX:-c++}" "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=1 -I"$repo/test/diagnostic_shim" -I"$repo/src" \
  "$repo/test/diagnostic_native_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/native"
"$build/native"
for diagnostics in 0 1; do
  "${CXX:-c++}" "${flags[@]}" -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DRISC_SLEEP_DIAGNOSTICS=$diagnostics \
    -I"$repo/test/diagnostic_shim" -I"$repo/src" "$repo/test/usb_sleep_recovery_test.cpp" \
    "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/recovery-$diagnostics"
  "$build/recovery-$diagnostics"
done
# Disabled recorder on UART/TinyUSB or the separate installer preserves its
# existing raw-Serial behavior and produces no native adapter symbols.
for transport in "-DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=1" "-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=0" "-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DRISC_OWNER_INSTALLER=1"; do
  "${CXX:-c++}" "${flags[@]}" $transport -DRISC_SLEEP_DIAGNOSTICS=0 -I"$repo/src" \
    -c "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/no-adapter.o"
  if nm -C "$build/no-adapter.o" | grep -q 'RiscDiagnostics::'; then
    echo 'unexpected disabled transport diagnostic adapter' >&2;exit 1
  fi
done
# Installer default recording remains selected, but new recovery is excluded.
printf '%s\n' '#include "ports/esp32s3/SleepDiagnostics.h"' \
  'static_assert(RISC_SLEEP_DIAGNOSTICS == 1 && RISC_DIAGNOSTIC_ADAPTER && !RISC_HWCDC_SLEEP_RECOVERY, "installer behavior changed");' | \
  "${CXX:-c++}" "${flags[@]}" -DRISC_OWNER_INSTALLER=1 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
  -I"$repo/src" -x c++ -fsyntax-only -

# Exercise unmodified, hash-pinned Arduino HWCDC against SDK/RTOS shims.
bash "$repo/test/hwcdc_pinned/run.sh" "$repo"
