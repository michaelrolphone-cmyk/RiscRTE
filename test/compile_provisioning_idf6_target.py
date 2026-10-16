#!/usr/bin/env python3
"""Compile composed provisioning with the pinned SDK6.1/GCC15 target flags.

A configured native build supplies SDK/config flags only. All Runtime include
paths and translation units are rebound to this checkout, and their hashes are
recorded. This compiles objects; it is neither a firmware nor hardware test.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compile-commands', type=Path, required=True)
p.add_argument('--idf', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
sdk = a.idf.resolve()
pin = 'fff9895c82d744c7237be8847347bdd1b07c6643'
assert subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip() == pin
entries = json.loads(a.compile_commands.read_text())
e = next(x for x in entries if x['file'].endswith('/NativeHardware.cpp'))
old = str(Path(e['file']).parents[3])
command = e.get('arguments') or shlex.split(e['command'])
compiler = command[0]
version = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]
assert '15.2.0' in version, version
config = configparser.ConfigParser()
config.read(root / 'platformio.ini')
source_commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
identity = '#define RISC_BUILD_VERSION "' + config['riscrte']['version'] + '"\n'
identity += '#define RISC_BUILD_SOURCE_SHA "' + source_commit + '"\n'
identity += '#define RISC_BUILD_IDENTITY "RTE_SOURCE=' + source_commit + '"\n'
(out / 'RiscBuildIdentity.h').write_text(identity)
flags = ['-I' + str(out)]
skip = False
for flag in command[1:]:
    if skip:
        skip = False
        continue
    if flag in ('-o', '-MF', '-MT', '-MQ'):
        skip = True
        continue
    if flag in ('-c', '-MD', '-MMD') or flag == e['file']:
        continue
    flags.append(flag.replace(old, str(root)))
flags += ['-DRISC_NATIVE_PROVISION_REQUEST=1', '-DRISC_NATIVE_PACKAGE_VOLUME=1',
          '-DRISC_ENABLE_HTTP=1', '-Werror', '-Werror=misleading-indentation', '-fstack-usage']
sources = ['src/runtime/provisioning/RequestQueue.cpp', 'src/runtime/provisioning/PreservingPolicy.cpp',
           'src/runtime/provisioning/Installer.cpp', 'src/ports/esp32s3/NvsKeyValue.cpp',
           'src/ports/esp32s3/NativeProvisionRequest.cpp', 'src/ports/esp32s3/NativePackageVolumeAdmission.cpp',
           'src/ports/esp32s3/NativeBankStore.cpp', 'src/ports/esp32s3/NativeBootstrap.cpp',
           'src/ports/esp32s3/CpuPort.cpp', 'src/ports/esp32s3/NativeHardware.cpp',
           'src/bootstrap/Runtime.cpp', 'src/main.cpp', 'test/native_http_target_compile.cpp']
commands = []
artifacts = []
def build(source, name=None, extra=()):
    name = name or Path(source).stem
    obj = out / (name + '.o')
    args = [compiler, *flags, *extra, '-c', str(root / source), '-o', str(obj)]
    commands.append(args)
    r = subprocess.run(args, cwd=e['directory'], text=True, capture_output=True)
    (out / (name + '.log')).write_text(r.stdout + r.stderr)
    if r.returncode:
        raise RuntimeError(name + '\n' + r.stdout + r.stderr)
    artifacts.append(obj)
    print('PASS', name, flush=True)
for source in sources:
    build(source)
for enabled, logs in ((1, 1), (0, 0), (0, 1)):
    build('src/main.cpp', f'main-request-{enabled}-logs-{logs}',
          ('-URISC_NATIVE_PROVISION_REQUEST', f'-DRISC_NATIVE_PROVISION_REQUEST={enabled}',
           '-URISC_STAGE_LOGS', f'-DRISC_STAGE_LOGS={logs}'))
build('src/ports/esp32s3/NativeProvisionRequest.cpp', 'request-disabled',
      ('-URISC_NATIVE_PROVISION_REQUEST', '-DRISC_NATIVE_PROVISION_REQUEST=0'))
nm = compiler.removesuffix('g++') + 'nm'
http_symbols = subprocess.check_output([nm, '-u', str(out / 'native_http_target_compile.o')], text=True)
assert not re.search(r'settimeofday|adjtime|clock_settime|sntp|rtc|Realtime', http_symbols, re.I), http_symbols
for name in ('esp_fill_random', 'getaddrinfo', 'recvfrom', 'sendto', 'esp_tls_init'):
    assert name in http_symbols, (name, http_symbols)
assert not subprocess.check_output([nm, '--defined-only', str(out / 'request-disabled.o')], text=True).strip()
(out / 'http-undefined.txt').write_text(http_symbols)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = dict(source_commit=source_commit, sdk_commit=pin, compiler=version, commands=commands,
                compile_database_sha256=sha(a.compile_commands),
                source_sha256={str(f.relative_to(root)): sha(f) for folder in ('src', 'sdk')
                               for f in sorted((root / folder).rglob('*')) if f.is_file()},
                object_sha256={f.name: sha(f) for f in artifacts})
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('PASS: actual SDK6 objects; request default-off exclusion; HTTP imports contain no global clock writes; no firmware/device claim')
