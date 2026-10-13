#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
for abi in 1 2 3; do
 flags=(); if [[ $abi != 1 ]]; then flags+=(-DRISC_PAIRED_APP_DATA=1); fi
 if [[ $abi == 3 ]]; then flags+=(-DRISC_PAIRED_APP_DATA_EXPANDED=1); fi
 c++ "${san[@]}" "${flags[@]}" -std=c++17 -Wall -Wextra -Werror \
   -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/lib/ArduinoJson/src" \
   "$repo/src/runtime/update/PairedBank.cpp" "$repo/src/bootstrap/Json.cpp" \
   "$repo/test/expanded_layout_test.cpp" -o "$build/test"
 "$build/test" "$build"
done
# Never permit an expanded build to silently omit its separate app-data mount.
if c++ -std=c++17 -DRISC_PAIRED_APP_DATA_EXPANDED=1 -I"$repo/src" -I"$repo/sdk/driver" \
 -x c++ -c -o "$build/invalid.o" - <<< '#include "runtime/update/PairedBank.h"' 2>"$build/invalid.log"; then
 echo 'ERROR: unbound expanded profile compiled' >&2; exit 1
fi
grep -q 'Expanded paired geometry requires' "$build/invalid.log"
