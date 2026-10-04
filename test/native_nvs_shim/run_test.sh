#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" -DESP_PLATFORM \
  -I"$repo/test/native_nvs_shim" -I"$repo/src" \
  -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" \
  "$repo/test/native_nvs_test.cpp" "$repo/src/ports/esp32s3/NvsKeyValue.cpp" \
  -o "$build/native_nvs_test"
# The production latch is intentionally never reset. Each result gets a fresh
# process, including each of Arduino's two destructive recovery conditions.
for result in ok no-free-pages new-version fail no-mem invalid-state not-initialized; do
  "$build/native_nvs_test" "$result"
done
# Embedded boot-store builds must not define another startup wrapper or depend
# on native NVS headers. Compile without shim/SDK include directories to prove it.
"${CXX:-c++}" "${flags[@]}" -DESP_PLATFORM -DRISC_EMBEDDED_BOOTSTORE \
  -c "$repo/src/ports/esp32s3/NvsKeyValue.cpp" -o "$build/embedded.o"
"${CXX:-c++}" -E -P -DESP_PLATFORM -DRISC_EMBEDDED_BOOTSTORE \
  "$repo/src/ports/esp32s3/NvsKeyValue.cpp" -o "$build/embedded.ii"
if grep -q '[^[:space:]]' "$build/embedded.ii"; then
  echo 'Embedded NVS translation unit is not empty' >&2
  exit 1
fi
echo 'Native NVS embedded-mode exclusion: PASS'
