#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime source is required}"
: "${RISCRTE_READER_ROOT:?Reader SDK is required}"
: "${RISCRTE_X4_ROOT:?X4 source is required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir -p "$build/minimal/test"
ln -s "$RISCRTE_X4_ROOT/minimal/drivers" "$build/minimal/drivers"
python3 - "$RISCRTE_X4_ROOT" "$root" "$build" <<'PY'
from pathlib import Path
import shlex
import sys
x4,root,out=map(Path,sys.argv[1:])
source=(x4/'minimal/test/frontlight_cpu_port_test.cpp').read_text()
assert source.count('int main()')==1
source=source.replace('int main()', 'void baseline_main()', 1)
source='#include <cstdlib>\n'+source+'\n'+(root/'test/display_startup_cases.inc').read_text()
(out/'minimal/test/frontlight_cpu_port_test.cpp').write_text(source)
script=(x4/'minimal/test/run_frontlight_cpu_port_test.sh').read_text()
# Redirect only the temporary regression runner. The frozen product checker
# still rejects divergent SDK copies and remains unmodified.
prepare='python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$runtime" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"'
assert script.count(prepare)==1
script=script.replace(prepare, 'python3 '+shlex.quote(str(root/'test/display_startup_sdk.py'))+' --runtime "$runtime" --reader "$RISCRTE_READER_ROOT" --x4 '+shlex.quote(str(x4))+' --output "$build/sdk"', 1)
assert script.count('cxxflags=(')==1
script=script.replace('cxxflags=(', 'cxxflags=(-DRISC_NATIVE_STORAGE_CUSTODY=1 ', 1)
(out/'minimal/test/run_frontlight_cpu_port_test.sh').write_text(script)
PY
python3 "$root/test/display_startup_sdk.py" --self-test
bash "$build/minimal/test/run_frontlight_cpu_port_test.sh"
