#!/usr/bin/env python3
"""Compile exact selected X4 provider sources through the production native port."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WATCH = '4966acee548e6cc3997289db206e996ed4c15cda'
DRIVERS = 'aa5ce0140102bf4d1039e5f208588a6793d06b28'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--watch', type=Path, required=True)
p.add_argument('--drivers', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--native-ref', help='Compare an immutable prior native HCI source without editing this checkout')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
receipts = {}
def read(repo, commit, source):
    data = subprocess.check_output(['git', '-C', repo, 'show', commit+':'+source])
    receipts[commit+':'+source] = hashlib.sha256(data).hexdigest()
    return data
def extract(repo, commit, sources, output):
    for source in sources:
        target = output/source
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(read(repo, commit, source))
def headers(repo, commit, folder):
    files = subprocess.check_output(['git', '-C', repo, 'ls-tree', '-r', '--name-only', commit, folder], text=True)
    return [path for path in files.splitlines() if path.endswith('.h')]
with tempfile.TemporaryDirectory(prefix='hci-production-') as temp:
    b = Path(temp)
    extract(a.watch, WATCH, ['drivers/twatch_ble/driver.c', *headers(a.watch, WATCH, 'include'), *headers(a.watch, WATCH, 'sdk')], b/'watch')
    extract(a.drivers, DRIVERS, ['Drivers/ble_sensors/driver.c', 'Drivers/ble_sensors/ble_scan_core.h', *headers(a.drivers, DRIVERS, 'sdk')], b/'drivers')
    # Providers use their own pinned SDK in separate translation units. Only
    # their public Bluetooth tables enter the Runtime fixture; one physical
    # copy avoids redeclaring SDK prefixes through different include roots.
    sdk = b/'include'; sdk.mkdir()
    for name in ['RiscBluetoothHostV1.h', 'RiscBluetoothSensorsV1.h', 'RiscTelemetryV1.h']:
        (sdk/name).write_bytes((b/'drivers/sdk/driver'/name).read_bytes())
    includes = [ROOT/'src', ROOT/'sdk/app', ROOT/'sdk/driver', ROOT/'sdk/hardware',
                sdk, ROOT/'lib/ArduinoJson/src', ROOT/'test/drivers/stubs']
    if a.native_ref:
        extract(ROOT, a.native_ref, ['src/ports/esp32s3/NativeHci.h', 'src/ports/esp32s3/HciBounds.h'], b/'baseline')
        includes.insert(0, b/'baseline/src')
    flags = ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers']
    if os.environ.get('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g']
    incs = ['-I'+str(path) for path in includes]
    objects = []
    for name, source in [('hci', b/'watch/drivers/twatch_ble/driver.c'), ('scanner', b/'drivers/Drivers/ble_sensors/driver.c')]:
        obj = b/(name+'.o'); objects.append(obj)
        provider_includes = [b/'watch/include', b/'watch/sdk/driver'] if name == 'hci' else [b/'drivers/sdk/driver']
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', *flags,
                        *['-I'+str(path) for path in provider_includes],
                        '-Dt5_driver_get=production_'+name+'_get', '-c', source, '-o', obj], check=True)
    sources = ['src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
               'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
               'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
               'src/ports/esp32s3/CpuPort.cpp', 'test/hci_scanner_burst_test.cpp']
    exe = a.output/'hci-production-scanner'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *flags, '-no-pie', '-rdynamic',
                    '-I'+str(ROOT/'test/native_hci_shim'), *incs, *map(str, objects),
                    *[str(ROOT/source) for source in sources], '-ldl', '-o', exe], check=True)
    (a.output/'source-receipt.json').write_text(json.dumps({'watch': WATCH, 'drivers': DRIVERS, 'native_ref': a.native_ref,
        'sha256': receipts, 'sanitized': os.environ.get('SANITIZE') == '1', 'hardware': 'not run'}, indent=2)+'\n')
    result = subprocess.run([exe], capture_output=True, text=True)
    print(result.stdout+result.stderr, end='')
    (a.output/'result.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
