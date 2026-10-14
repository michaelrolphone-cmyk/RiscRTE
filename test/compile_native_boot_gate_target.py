#!/usr/bin/env python3
"""Compile actual main.cpp with the queued-handoff gate enabled and disabled."""
from pathlib import Path
import argparse
import json
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--packages', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.output.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'test/compile_native_http_target.py'),
                '--packages', str(a.packages), '--output', str(a.output / 'http')], check=True)
commands = json.loads((a.output / 'http/commands.json').read_text())
base = commands[-1]
framework = a.packages / 'framework-arduinoespressif32'
(a.output / 'RiscBuildIdentity.h').write_text('#pragma once\n#define RISC_BUILD_IDENTITY "RTE_SOURCE=boot-gate-compile"\n#define RISC_BUILD_VERSION "0.0.0"\n')
extra = ['-Wno-misleading-indentation', '-DRISC_PAIRED_BANKS=1', '-DARDUINO_USB_CDC_ON_BOOT=1',
         '-DARDUINO_USB_MODE=1', '-DARDUINO=10812', '-DARDUINO_ARCH_ESP32', '-DARDUINO_ESP32S3_DEV',
         '-DRISC_TARGET="esp32s3"', '-include', 'sdkconfig.h']
extra += ['-I' + str(d) for d in [a.output, root, root / 'include', root / 'lib/elf_loader/include',
                                framework / 'cores/esp32', framework / 'variants/esp32s3']]
main_commands = []
for requests, logs in [(0, 0), (1, 0), (1, 1)]:
    command = [*base[:-4], *extra, '-DRISC_NATIVE_PROVISION_REQUEST=' + str(requests),
               '-DRISC_STAGE_LOGS=' + str(logs), '-c', str(root / 'src/main.cpp'), '-o',
               str(a.output / ('main-request-' + str(requests) + '-logs-' + str(logs) + '.o'))]
    main_commands.append(command)
    result = subprocess.run(command)
    if result.returncode:
        raise SystemExit(result.returncode)
(a.output / 'main-commands.json').write_text(json.dumps(main_commands, indent=2) + '\n')
print('PASS: actual ESP32-S3 main.cpp; request-enabled gate with logs off/on; unchanged request-disabled path; production mbedTLS configuration. Compile only, no device qualification.')
