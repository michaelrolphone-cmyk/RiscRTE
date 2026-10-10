#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsyntax-only -I"$repo/sdk/app" "$repo/test/resident_callback_prefix_abi.c"
export RESIDENT_LOADING=1
export RESIDENT_SCENARIOS="normal file-open loading-chain loading-busy loading-chain-busy loading-retained loading-invalid loading-native-retained loading-provider-retained loading-old-prefix loading-partial-prefix loading-null load-failure init-failure descriptor child-retained"
bash "$repo/test/run_resident_shell_test.sh"
export RESIDENT_LEGACY_SCENARIOS="direct child-launch-legacy legacy-launch-resident repeated-chain file-resident-legacy file-resident-legacy-init-failure file-resident-legacy-load-failure file-legacy-resident file-legacy-resident-init-failure file-legacy-resident-load-failure loading-resume-busy file-legacy-resident-loading-busy file-resident-legacy-loading-busy child-fini-retained child-grant-retained child-unload-retained"
bash "$repo/test/run_resident_legacy_test.sh"
