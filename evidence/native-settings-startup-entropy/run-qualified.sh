#!/bin/bash
set -euo pipefail
cd /workspace/shared/x4-settings-startup-entropy-20261012
export BACKEND_SDK=/workspace/shared/x4-littlefs-restore-integration-evidence-20261012/factory-normal/sdk ESP_LITTLEFS_SOURCE=/workspace/shared/x4-settings-native-backend-evidence-20261012/normal/vendor LITTLEFS_SOURCE=/workspace/shared/x4-settings-native-backend-evidence-20261012/normal/vendor/littlefs STARTUP_ENTROPY=1
E=/workspace/shared/x4-settings-startup-entropy-evidence-20261012
BUILD_DIR=$E/backend-final-normal python3 test/run_native_settings_backend_test.py > $E/backend-final-normal.log 2>&1
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 BUILD_DIR=$E/backend-final-asan python3 test/run_native_settings_backend_test.py > $E/backend-final-asan.log 2>&1
python3 test/compile_native_settings_backend_target.py /workspace/shared/x4-https-099-20261012/native-preflight/compile_commands.json $E/target-final > $E/target-final.log 2>&1
