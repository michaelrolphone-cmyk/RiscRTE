#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime source is required}"
: "${RISCRTE_READER_ROOT:?Reader SDK is required}"
: "${RISCRTE_X4_ROOT:?X4 source is required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir -p "$build/minimal/test"
ln -s "$RISCRTE_X4_ROOT/minimal/drivers" "$build/minimal/drivers"
ln -s "$RISCRTE_X4_ROOT/minimal/scripts" "$build/minimal/scripts"
python3 - "$RISCRTE_X4_ROOT" "$root" "$build" <<'PY'
from pathlib import Path
import sys
x4,root,out=map(Path,sys.argv[1:])
source=(x4/'minimal/test/frontlight_cpu_port_test.cpp').read_text()
assert source.count('int main()')==1
source=source.replace('int main()', 'void baseline_main()', 1)
source='#include <cstdlib>\n'+source+'\n'+(root/'test/display_startup_cases.inc').read_text()
(out/'minimal/test/frontlight_cpu_port_test.cpp').write_text(source)
script=(x4/'minimal/test/run_frontlight_cpu_port_test.sh').read_text()
assert script.count('cxxflags=(')==1
script=script.replace('cxxflags=(', 'cxxflags=(-DRISC_NATIVE_STORAGE_CUSTODY=1 ', 1)
(out/'minimal/test/run_frontlight_cpu_port_test.sh').write_text(script)
PY
bash "$build/minimal/test/run_frontlight_cpu_port_test.sh"
