#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
incs=(-I"$repo/src" -I"$repo/src/bootstrap" -I"$repo/sdk/app" -I"$repo/sdk/driver"
      -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src")
printf '%s\n' '#include "bootstrap/Runtime.h"' > "$build/selected.cpp"
for atomic in 0 1;do
  for lifecycle in 0 1;do
    "${CXX:-c++}" -std=c++17 -fsyntax-only "${incs[@]}" \
      -DRISC_RUNTIME_ATOMIC_KEY_VALUE="$atomic" -DRISC_NATIVE_SETTINGS_LIFECYCLE="$lifecycle" "$build/selected.cpp"
  done
done
# Reintroduce the shipped collision only in a private temporary header. This
# must fail at the guard, rather than accidentally passing a runtime test with
# the atomic broker absent from the selected build.
sed 's/AppCatalogDriver=-13/AppCatalogDriver=-11/' "$repo/src/bootstrap/Runtime.h" > "$build/Runtime.h"
printf '%s\n' '#include "Runtime.h"' > "$build/collision.cpp"
for lifecycle in 0 1;do
  if "${CXX:-c++}" -std=c++17 -fsyntax-only "${incs[@]}" \
       -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -DRISC_NATIVE_SETTINGS_LIFECYCLE="$lifecycle" \
       "$build/collision.cpp" > "$build/collision.log" 2>&1;then
    echo 'Reintroduced atomic/catalog dispatch collision unexpectedly compiled' >&2;exit 1
  fi
  grep -q 'Native app capability dispatch tags must be distinct and negative' "$build/collision.log"
done
echo 'Native app capability tags: all four feature combinations compile; atomic/catalog collision rejected with lifecycle off/on PASS'
