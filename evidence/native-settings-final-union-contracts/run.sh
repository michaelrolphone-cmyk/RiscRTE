#!/bin/bash
set -euo pipefail
cd /workspace/shared/x4-settings-persistence-20261012
E=/workspace/shared/x4-settings-final-union-contracts-20261012
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 SANITIZERS=address,undefined NO_PIE=1
for mode in 0 1; do
 export SANITIZE=$mode
 for script in run_native_settings_startup_entropy_test.py run_native_atomic_key_value_test.sh run_native_settings_coordinator_test.sh run_native_first_sd_checkpoint_test.sh run_native_settings_availability_test.sh test_native_settings_startup.py; do
  echo START $mode $script
  if [[ $script == *.py ]]; then python3 test/$script > $E/$mode-$script.log 2>&1;else bash test/$script > $E/$mode-$script.log 2>&1;fi
  echo PASS $mode $script
 done
done
