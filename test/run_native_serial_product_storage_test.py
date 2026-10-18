#!/usr/bin/env python3
"""Actual startup mapping and Runtime admission with synthetic storage, no device I/O."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
flags = ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-fno-omit-frame-pointer']
if os.environ.get('SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
includes = ['-I'+str(root/x) for x in ('', 'src', 'sdk/app', 'sdk/driver', 'sdk/hardware',
    'lib/ArduinoJson/src', 'test/drivers/stubs', 'test/native_sleep_diagnostics_idf6_shim', 'test/native_platform_shim')]
selection = ['-DRISC_NATIVE_SERIAL_FILES=1', '-DRISC_NATIVE_SERIAL_SD=1', '-DRISC_NATIVE_ESP_IDF=1']
commands = []
local_records={str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest()
    for folder in ('src','sdk','lib/ArduinoJson/src','test/drivers/stubs','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim')
    for x in (root/folder).rglob('*') if x.is_file() and x.suffix in ('.h','.hpp','.c','.cpp','.inc')}
for name in ('test/native_serial_product_storage_test.cpp','test/native_serial_product_provider.c','test/run_native_serial_product_storage_test.py','test/storage_files_adapter_test.cpp','test/RuntimeFileFixture.h','test/fixtures/atomic_key_value_app.c'):
    local_records[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
def run(command, **kwargs):
    command = [str(x) for x in command]
    commands.append(command)
    return subprocess.run(command, check=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='native-serial-product-') as temp:
    build = Path(temp)
    probe = build/'config.cpp'
    probe.write_text('#include "ports/esp32s3/NativeSerialStorage.h"\n')
    invalid = (
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"'],
        ['-DRISC_NATIVE_SERIAL_SD_INSTANCE=9'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER=""', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=9'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd\\0alias"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=9'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="'+('x'*96)+'"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=9'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=0'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=-1'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=9.5'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=true'],
        ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE=2147483648ULL'],
    )
    for definitions in invalid:
        command = [cxx, '-std=c++17', *includes, *selection, *definitions, '-fsyntax-only', str(probe)]
        commands.append(command)
        result = subprocess.run(command, text=True, capture_output=True)
        assert result.returncode != 0 and 'Serial SD' in result.stderr, result.stderr
    print('Incomplete, empty/embedded-NUL/oversized provider and out-of-range product selection rejected PASS', flush=True)
    run([cc, '-std=c11', *flags, *includes, '-shared', '-fPIC', '-fvisibility=hidden',
         root/'test/fixtures/atomic_key_value_app.c', '-o', build/'default.elf'])
    sources = (
        'src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
        'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
        'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
        'src/runtime/serial/Files.cpp', 'src/ports/esp32s3/NativeSerialFiles.cpp')
    objects = []
    for name in sources:
        obj = build/(Path(name).stem+'.o')
        run([cxx, '-std=c++17', *flags, *includes, *selection, '-c', root/name, '-o', obj])
        objects.append(obj)
    cases = ('healthy', 'absent-media', 'refresh-refused', 'refresh-retained', 'failed-close', 'failed-release',
             'unadmitted', 'wrong-provider', 'wrong-instance', 'wrong-api', 'wrong-capability', 'ambiguous', 'invalid-board')
    for profile, provider, instance, explicit in (
            ('legacyWave12', 'sdmmc-fatfs', 12, False), ('X4SD9', 'x4pro-sd', 9, True)):
        product = ['-DRISC_NATIVE_SERIAL_SD_PROVIDER="'+provider+'"', '-DRISC_NATIVE_SERIAL_SD_INSTANCE='+str(instance)] if explicit else []
        fixture = ['-DTEST_PRODUCT_PROVIDER="'+provider+'"', '-DTEST_PRODUCT_INSTANCE='+str(instance)]
        run([cc, '-std=c11', *flags, *includes, *fixture, '-shared', '-fPIC', '-fvisibility=hidden',
             root/'test/native_serial_product_provider.c', '-o', build/'provider.elf'])
        run([cxx, '-std=c++17', *flags, *includes, *selection, *product, *fixture, '-rdynamic', '-no-pie', '-pthread',
             root/'src/ports/esp32s3/NativeSerialStorage.cpp', root/'test/native_serial_product_storage_test.cpp',
             *objects, '-ldl', '-o', build/'test'])
        for case in cases:
            run([build/'test', build, case])
        print(profile+': shared production startup binding, exact graph admission, NS12 and custody PASS', flush=True)
    for name,expected in local_records.items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest()==expected, 'Input changed during product qualification: '+name
    if os.environ.get('RECEIPT'):
        Path(os.environ['RECEIPT']).write_text(json.dumps(dict(commands=commands, cases=cases,
            profiles=['legacyWave12', 'X4SD9'], invalid_configurations=len(invalid),
            sanitized=os.environ.get('SANITIZE')=='1', runtime_sources=local_records, immutable_inputs_verified=True, sanitizer_environment={key:os.environ.get(key) for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS')},
            limits='Host production startup/Runtime/serial integration with a synthetic hardware provider and memory backend; no X4 driver or physical device qualification.'), indent=2)+'\n')
