#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" "$repo/test/performance_recorder_test.cpp" -o "$build/test"
"$build/test"
printf '%s\n' '#include "RiscPerformanceV1.h"' 'int main(void){return (int)risc_perf_trace_v1(0,0,1,1);}' | "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/app" -x c -fsyntax-only -
# Production opt-out must have neither recorder RAM nor observer side effects,
# even without optimization and even if configure() is asked to enable tracing.
cat > "$build/disabled.cpp" <<'CPP'
#include "diagnostics/Performance.h"
#include <cassert>
static unsigned clocks=0,owners=0;
static uint64_t clockProbe(){++clocks;return 7;}
static bool ownerProbe(){++owners;return true;}
int main(){
  RiscPerf::configure(clockProbe,ownerProbe,true);
  assert(!RiscPerf::allowed() && RiscPerf::now()==0);
  assert(RiscPerf::identity("app")==0);
  RiscPerf::invocation("app");RiscPerf::emit(12);
  assert(RiscPerf::interaction(0,1,1)==0);
  RiscPerf::finish(12,13,0);
  {RiscPerf::Scope phase(10,11);RiscPerf::AggregateScope aggregate(26,2);}
  assert(clocks==0 && owners==0);
}
CPP
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O0 \
 -DESP_PLATFORM=1 -DRISC_PERFORMANCE_TRACE=0 -I"$repo/src" \
 -c "$build/disabled.cpp" -o "$build/disabled.o"
if nm -C "$build/disabled.o" | grep -E 'RiscPerf::(data|clockUs|ownerTask|enabled)' > "$build/unexpected-symbols"; then
 cat "$build/unexpected-symbols" >&2
 echo 'disabled production recorder retains observer state' >&2
 exit 1
fi
"${CXX:-c++}" "$build/disabled.o" -o "$build/disabled"
"$build/disabled"
echo 'disabled production recorder: no state symbols, clock calls or owner calls'
