#!/usr/bin/env python3
"""Qualify settings atomic placement with real SDK6.1 Xtensa objects only."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('compile_commands', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
entry = next(e for e in json.loads(a.compile_commands.read_text()) if e['file'].endswith('/NativeHardware.cpp'))
base = entry.get('arguments') or shlex.split(entry['command'])
compiler = Path(base[0])
nm = compiler.with_name(compiler.name.replace('g++', 'nm'))
objdump = compiler.with_name(compiler.name.replace('g++', 'objdump'))
probe = out / 'atomic-policy-probe.cpp'
probe.write_text('''#include "test/native_settings_worker_layout_probe.cpp"
static_assert(ATOMIC_INT_LOCK_FREE==1,"SDK PSRAM workaround must remain active");
static_assert(CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND==1,"Do not disable SDK workaround");
static_assert(sizeof(std::atomic<TaskHandle_t>)==4,"Target task handle publication must be32 bits");
''')
manifest = {'commands': [], 'objects': {}}
cases = [
    ('worker', root/'src/ports/esp32s3/NativeSettingsWorker.cpp', 1, 0),
    ('worker-mount', root/'src/ports/esp32s3/NativeSettingsWorker.cpp', 1, 1),
    ('worker-api', root/'test/native_settings_worker_target_probe.cpp', 1, 0),
    ('worker-layout-policy', probe, 1, 0),
    ('worker-default-off', root/'src/ports/esp32s3/NativeSettingsWorker.cpp', 0, 0),
]
for name, source, enabled, mount in cases:
    target = out / (name + '.o')
    command = list(base)
    command[command.index('-c')+1] = str(source)
    command[command.index('-o')+1] = str(target)
    for option, value in (('-MF', str(target)+'.d'), ('-MT', str(target))):
        if option in command:
            command[command.index(option)+1] = value
    command += ['-I'+str(root), '-Werror', '-URISC_NATIVE_SETTINGS_WORKER', f'-DRISC_NATIVE_SETTINGS_WORKER={enabled}',
                '-URISC_NATIVE_APP_DATA_MOUNT', f'-DRISC_NATIVE_APP_DATA_MOUNT={mount}']
    if mount:
        command += ['-DRISC_PAIRED_APP_DATA']
    manifest['commands'].append(command)
    result = subprocess.run(command, cwd=entry['directory'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out/(name+'.log')).write_text(result.stdout)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(name+' failed')
    symbols = subprocess.check_output([str(nm), '-u', str(target)], text=True)
    (out/(name+'.symbols')).write_text(symbols)
    assert '__atomic_load_8' not in symbols and '__atomic_store_8' not in symbols and '__atomic_compare_exchange_8' not in symbols
    assert 'vTaskDelete' not in symbols and 'esp_vfs_littlefs' not in symbols and 'nvs_flash_init' not in symbols
    if name in ('worker', 'worker-mount', 'worker-layout-policy'):
        sections = subprocess.check_output([str(objdump), '-t', str(target)], text=True)
        (out/(name+'.sections')).write_text(sections)
        assert any('registryE' in row and '.dram1' in row for row in sections.splitlines())
        assert 'xTaskCreateStatic' in symbols and 'heap_caps_malloc' in symbols
    if not enabled:
        assert not subprocess.check_output([str(nm), str(target)], text=True).strip()
    manifest['objects'][target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
    print(name+': actual SDK6.1 target PASS', flush=True)
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Five real settings target objects: DRAM registry,32-bit atomics, SDK PSRAM workaround retained, default-off empty PASS')
