#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O2 -Wall -Wextra -Werror
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware"
 -I"$repo/test/native_platform_shim")
for variant in host host-testing esp esp-testing; do
 defines=()
 case "$variant" in
  host-testing) defines=(-DRISC_STREAM_HOST_TESTING);;
  esp) defines=(-DESP_PLATFORM);;
  esp-testing) defines=(-DESP_PLATFORM -DRISC_STREAM_HOST_TESTING);;
 esac
 "${CXX:-c++}" "${flags[@]}" "${defines[@]}" -c "$repo/src/runtime/streams/AppStreamSessions.cpp" -o "$build/$variant.o"
 "${NM:-nm}" -C "$build/$variant.o" > "$build/$variant.symbols"
 if [[ "$variant" == host-testing ]]; then
  grep -q 'RuntimeStreams::Testing::setSessionClock' "$build/$variant.symbols"
  grep -q 'RuntimeStreams::Testing::setSessionTimingObserver' "$build/$variant.symbols"
 else
  if grep -Eq 'setSessionClock|setSessionTimingObserver|sessionClock|sessionTimingObserver' "$build/$variant.symbols"; then
   echo "Unexpected test timing seam in $variant" >&2; exit 1
  fi
 fi
 if [[ "$variant" == host* ]]; then
  grep -q 'steady_clock::now' "$build/$variant.symbols"
 else
  grep -q 'esp_timer_get_time' "$build/$variant.symbols"
  if grep -q 'steady_clock' "$build/$variant.symbols"; then exit 1; fi
 fi
 echo "$variant: production clock selection and test-hook boundary PASS"
done
# Even an accidentally supplied host-test define cannot change the ESP object.
cmp "$build/esp.o" "$build/esp-testing.o"
echo 'ESP objects identical with/without RISC_STREAM_HOST_TESTING PASS'
