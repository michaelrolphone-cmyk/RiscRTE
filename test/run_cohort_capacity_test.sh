#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
bash "$repo/test/run_runtime_limits_test.sh"
"${PYTHON:-python3}" "$repo/test/runtime_capacity_proof_test.py"
export RISC_COHORT_PROVIDER_CAPACITY=29
for suite in runtime provider_graph_v2 demand_retention cohort_runtime native_bank_provider_admission provider_queue_host provider_sync paired_runtime_memory;do
 bash "$repo/test/run_${suite}_test.sh"
done
