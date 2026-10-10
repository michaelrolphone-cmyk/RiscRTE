#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
# Source-only host layout comparison. No Xtensa compiler or target linker runs.
baseline="${RUNTIME_CAPACITY_BASELINE:-5a5c14de2dbcf189d1ca4912dfde1773a4d9813d}"
mkdir "$build/baseline"
git -C "$repo" archive "$baseline" | tar -x -C "$build/baseline"
for version in baseline current;do
  source="$repo";if [[ "$version" == baseline ]];then source="$build/baseline";fi
  for profile in cohort legacy expanded;do
    if [[ "$version" == baseline && "$profile" == expanded ]];then continue;fi
    override=()
    if [[ "$profile" == expanded ]];then override=(-DRISC_COHORT_PROVIDER_CAPACITY=29);fi
    if [[ "$profile" == legacy ]];then
      mkdir -p "$build/legacy/runtime"
      sed 's/^#if defined(ESP_PLATFORM).*$/#if 1/' "$source/src/runtime/RuntimeLimits.h" > "$build/legacy/runtime/RuntimeLimits.h"
      override=(-I"$build/legacy")
    fi
    c++ -std=c++17 -Wall -Wextra -Werror "${override[@]}" \
      -I"$source/src" -I"$source/sdk/app" -I"$source/sdk/driver" -I"$source/sdk/hardware" \
      -I"$source/lib/ArduinoJson/src" -I"$source/test/drivers/stubs" \
      "$repo/test/cohort_capacity_memory_test.cpp" -o "$build/test"
    printf '%s %s ' "$version" "$profile";"$build/test"
  done
done
