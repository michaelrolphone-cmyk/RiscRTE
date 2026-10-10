#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cat > "$build/test.cpp" <<'CPP'
#include "runtime/RuntimeLimits.h"
static_assert(RiscLimits::Apps==EXPECTED_APPS,"app capacity");
static_assert(RiscLimits::Providers==EXPECTED_PROVIDERS,"provider capacity");
static_assert(RiscLimits::Grants==EXPECTED_GRANTS,"grant capacity");
int main(){}
CPP
base=(-std=c++17 -Wall -Wextra -Werror -I"$repo/src" "$build/test.cpp" -o "$build/test")
legacy=(-DEXPECTED_APPS=19 -DEXPECTED_PROVIDERS=17 -DEXPECTED_GRANTS=32)
cohort=(-DEXPECTED_APPS=24 -DEXPECTED_PROVIDERS=26 -DEXPECTED_GRANTS=42)
for flag in '' '-DRISC_EMBEDDED_BOOTSTORE=1';do
 c++ "${base[@]}" -DESP_PLATFORM=1 ${flag:+$flag} "${legacy[@]}";"$build/test"
done
for flag in '-DRISC_PAIRED_BANKS=1' '-DRISC_RUNTIME_METADATA_PSRAM=1';do
 c++ "${base[@]}" -DESP_PLATFORM=1 "$flag" "${cohort[@]}";"$build/test"
done
c++ "${base[@]}" "${cohort[@]}";"$build/test"
echo 'Legacy static19/17/32 and PSRAM cohort24/26/42 capacities PASS'
expanded=(-DEXPECTED_APPS=24 -DEXPECTED_PROVIDERS=29 -DEXPECTED_GRANTS=45)
for flag in '-DRISC_PAIRED_BANKS=1' '-DRISC_RUNTIME_METADATA_PSRAM=1';do
 c++ "${base[@]}" -DESP_PLATFORM=1 "$flag" -DRISC_COHORT_PROVIDER_CAPACITY=29 "${expanded[@]}";"$build/test"
done
c++ "${base[@]}" -DRISC_COHORT_PROVIDER_CAPACITY=29 "${expanded[@]}";"$build/test"
for capacity in 0 17 25 27 28 30 256;do
 if c++ "${base[@]}" -DRISC_COHORT_PROVIDER_CAPACITY="$capacity" "${cohort[@]}" >"$build/rejected" 2>&1;then
  echo "Unsupported capacity $capacity compiled";exit 1
 fi
 grep -q 'RISC_COHORT_PROVIDER_CAPACITY must be 26 or 29' "$build/rejected"
done
if c++ "${base[@]}" -DESP_PLATFORM=1 -DRISC_COHORT_PROVIDER_CAPACITY=29 "${legacy[@]}" >"$build/rejected" 2>&1;then
 echo 'Expanded legacy capacity compiled';exit 1
fi
grep -q 'Expanded cohort capacity requires PSRAM Runtime metadata' "$build/rejected"
echo 'Explicit PSRAM cohort24/29/45 accepted; unsupported and legacy opt-ins rejected PASS'
