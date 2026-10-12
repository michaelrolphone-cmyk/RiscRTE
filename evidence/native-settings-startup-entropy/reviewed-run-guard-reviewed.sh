#!/bin/bash
set -euo pipefail
cd /workspace/shared/x4-settings-startup-entropy-20261012
E=/workspace/shared/x4-settings-startup-entropy-evidence-20261012
ARGS=(--x4 /workspace/shared/x4-first-sd-capacity-20261012 --drivers /workspace/shared/drivers-first-sd-capacity-20261012 --reference-reader /workspace/shared/persistence-recovery-20261011/repos/Reader --x4-revision 0a10bd15e3693768683fd41870f5d438ed57ccfe --drivers-revision ee244590d3c835af012776ce2480e47b25e23477)
python3 test/run_native_first_sd_consumers_test.py "${ARGS[@]}" --output $E/first-guard-reviewed-normal > $E/first-guard-reviewed-normal.log 2>&1
ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_first_sd_consumers_test.py "${ARGS[@]}" --sanitize address,undefined --output $E/first-guard-reviewed-asan > $E/first-guard-reviewed-asan.log 2>&1
python3 test/run_native_first_sd_consumers_test.py "${ARGS[@]}" --checked-store --output $E/first-guard-reviewed-store-normal > $E/first-guard-reviewed-store-normal.log 2>&1
ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_first_sd_consumers_test.py "${ARGS[@]}" --checked-store --sanitize address,undefined --output $E/first-guard-reviewed-store-asan > $E/first-guard-reviewed-store-asan.log 2>&1
