#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
for selection in 0 1;do
 NATIVE_PROVIDER_ADMISSION_ONLY=1 NATIVE_PROVIDER_TEST_SELECTION="$selection" bash "$repo/test/run_native_bank_test.sh"
done
