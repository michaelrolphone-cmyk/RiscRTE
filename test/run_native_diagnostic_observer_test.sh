#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O1 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g)
fi
for profile in present absent absent-read absent-drain observer-only disabled; do
 define=(-DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1)
 stage=1;usb=1
 if [[ $profile == absent ]]; then define+=(-DTEST_ABSENT_OBSERVER=1 -DTEST_ABSENT_READ=1 -DTEST_ABSENT_DRAIN=1); fi
 if [[ $profile == absent-read ]]; then define+=(-DTEST_ABSENT_READ=1); fi
 if [[ $profile == absent-drain ]]; then define+=(-DTEST_ABSENT_DRAIN=1); fi
 if [[ $profile == observer-only ]]; then stage=0;usb=0; fi
 if [[ $profile == disabled ]]; then define=(-DRISC_NATIVE_DIAGNOSTIC_OBSERVER=0); fi
 "${CXX:-c++}" "${flags[@]}" "${define[@]}" -DRISC_STAGE_LOGS=$stage -DRISC_SLEEP_DIAGNOSTICS=0 -DRISC_PERFORMANCE_TRACE=0 \
  -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=$usb \
  -I"$repo/test/stage_shim" -I"$repo/test/diagnostic_shim" -I"$repo/src" -I"$repo/sdk/driver" \
  "$repo/test/native_diagnostic_observer_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/test"
 "$build/test"
 if [[ $profile == disabled ]] && nm -C "$build/test" | grep -E 'RiscDiagnostics::(nativeSource|.*readSource|.*NativeStorageDrain)'; then
  echo 'disabled native diagnostics retains source/drain code' >&2;exit 1
 fi
 echo "native diagnostic observer $profile PASS"
done
