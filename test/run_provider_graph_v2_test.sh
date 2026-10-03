#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/hardware" -I"$repo/sdk/driver")
cc "${flags[@]}" -DFIXTURE_ID='"fixture-root"' -DFIXTURE_CAPABILITY='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/root.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-root-alt"' -DFIXTURE_CAPABILITY='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/root-alt.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-child"' -DFIXTURE_CAPABILITY='"cap.child"' \
  -DFIXTURE_REQUIRE='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/child.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-other"' -DFIXTURE_CAPABILITY='"cap.other"' \
  -DFIXTURE_REQUIRE='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/other.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_graph_v2_test.cpp" -ldl -o "$build/graph-test"
"$build/graph-test" "$build/root.so" "$build/child.so" "$build/other.so" "$build/root-alt.so"

# Registration owns metadata and candidate bytes, independent of caller mutation.
c++ -std=c++17 -Wall -Wextra -Werror "${san[@]}" \
  -I"$repo/sdk/hardware" -I"$repo/sdk/driver" -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_owned_spec_v2_test.cpp" -ldl -o "$build/ownership-test"
"$build/ownership-test" "$build/root.so"

# Retained start() dependency pointers survive activation, quiesce and stop.
cc "${flags[@]}" "${san[@]}" \
  "$repo/test/drivers/provider_dependency_retention_fixture.c" -o "$build/retaining.so"
c++ -std=c++17 -Wall -Wextra -Werror "${san[@]}" \
  -I"$repo/sdk/hardware" -I"$repo/sdk/driver" -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_dependency_lifetime_v2_test.cpp" \
  -ldl -o "$build/dependency-lifetime-test"
ASAN_OPTIONS=detect_stack_use_after_return=1 "$build/dependency-lifetime-test" \
  "$build/root.so" "$build/retaining.so"

# Failed start retains mapped code/dependencies until quiescence retry.
cc "${flags[@]}" "$repo/test/drivers/provider_failed_start_fixture.c" -o "$build/failed-start.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_failed_start_recovery_v2_test.cpp" \
  -ldl -o "$build/recovery-test"
"$build/recovery-test" "$build/root.so" "$build/failed-start.so"

# Recover a grantless failed activation by exact installed ID while another
# provider sharing its dependency holds a live grant. Global graph shutdown
# or speculative dependency release would invalidate the unrelated provider.
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_targeted_recovery_v2_test.cpp" \
  -ldl -o "$build/targeted-recovery-test"
"$build/targeted-recovery-test" "$build/root.so" "$build/failed-start.so" "$build/other.so"

# Failed quiescence revokes grants and does not force-unmap hardware.
cc "${flags[@]}" -DFIXTURE_ID='"fixture-retry"' \
  -DFIXTURE_CAPABILITY='"cap.retry"' -DFIXTURE_QUIESCE_FAIL_ONCE \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/retry.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_failed_teardown_v2_test.cpp" \
  -ldl -o "$build/teardown-test"
"$build/teardown-test" "$build/retry.so"

# An owner destroying a graph with perpetually mapped hardware MUST fail stop
# instead of returning with dangling graph-owned API/dependency addresses.
cc "${flags[@]}" -DFIXTURE_ID='"fixture-stuck"' \
  -DFIXTURE_CAPABILITY='"cap.stuck"' -DFIXTURE_QUIESCE_FAIL \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/stuck.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/hardware" -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_graph_destruction_guard_v2_test.cpp" \
  -ldl -o "$build/destruction-test"
"$build/destruction-test" "$build/stuck.so"

