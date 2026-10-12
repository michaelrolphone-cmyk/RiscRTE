#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" "$repo/test/crown_recovery_policy_test.cpp" -o "$build/policy"
"$build/policy"
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" -I"$repo/test/native_crown_shim" \
 -DESP_PLATFORM -DRISC_NATIVE_RECOVERY_AXP2101=1 -DRISC_RECOVERY_I2C_PORT=0 -DRISC_RECOVERY_SDA=10 -DRISC_RECOVERY_SCL=11 \
 "$repo/src/ports/esp32s3/NativeCrownRecovery.cpp" "$repo/test/native_crown_recovery_test.cpp" -o "$build/native"
for scenario in deep-fast deep-held deep-ext0 deep-gpio deep-timer deep-cold deep-motion deep-read-failure prepare-race exit-timeout exit-bus-stuck home-short clean-exit held held-at-boot release bus-error sleep task-failure wrong-chip;do "$build/native" "$scenario";done
