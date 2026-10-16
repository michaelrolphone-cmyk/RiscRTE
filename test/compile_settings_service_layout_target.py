#!/usr/bin/env python3
"""Measure actual target object layout and retain exact inputs and commands.

Usage: script compile_commands.json[.gz] BEFORE_SOURCE OUTPUT
The before tree must predate SettingsService.h. No firmware link is claimed.
"""
import argparse, gzip, hashlib, json, pathlib, shlex, subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('commands', type=pathlib.Path)
p.add_argument('before', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--after', type=pathlib.Path, help='exact alternative settings source tree')
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)
root = a.after.resolve() if a.after else pathlib.Path(__file__).resolve().parents[1]
with (gzip.open if a.commands.suffix == '.gz' else open)(a.commands, 'rt') as f:
    rows = json.load(f)
row = next(r for r in rows if r['file'].endswith('/src/bootstrap/Runtime.cpp'))
original = row['file'][:-len('/src/bootstrap/Runtime.cpp')]
report = {'commands_sha256': hashlib.sha256(a.commands.read_bytes()).hexdigest(), 'measurements': {}}
for label, source in [('before', a.before.resolve()), ('after', root)]:
    code = '#include "bootstrap/Runtime.h"\nextern "C" {char resource_Runtime[sizeof(RiscBoot::Runtime)];char resource_AtomicBinding[sizeof(RiscBoot::Runtime::AtomicBinding)];}\n'
    if label == 'after':
        code += '#include "bootstrap/LegacySettingsProvider.h"\nextern "C" {char resource_LegacySettingsSession[sizeof(RiscBoot::LegacySettingsSession)];char resource_AtomicSettingsEndpoint[sizeof(RiscBoot::AtomicSettingsEndpoint)];}\n'
    probe = a.output / (label + '.cpp')
    probe.write_text(code)
    obj, deps = probe.with_suffix('.o'), probe.with_suffix('.d')
    command, skip = [], False
    for value in shlex.split(row['command']):
        if skip:
            skip = False
            continue
        if value == '-o':
            skip = True
            continue
        if value == row['file']:
            value = str(probe)
        elif original in value and original + '/idf/' not in value:
            value = value.replace(original, str(source))
        command.append(value)
    command += ['-fno-access-control', '-URISC_RUNTIME_PREFERENCE_STORE', '-URISC_RUNTIME_STORAGE_FILES', '-DRISC_RUNTIME_PREFERENCE_STORE=1', '-DRISC_RUNTIME_STORAGE_FILES=1', '-MMD', '-MF', str(deps), '-o', str(obj)]
    subprocess.run(command, cwd=row['directory'], check=True)
    compiler = next(value for value in command if pathlib.Path(value).name.endswith('g++'))
    nm_command = [compiler.removesuffix('g++') + 'nm', '-S', str(obj)]
    nm = subprocess.check_output(nm_command, text=True)
    sizes = {v.split()[-1]: int(v.split()[1], 16) for v in nm.splitlines() if v.split()[-1].startswith('resource_')}
    inputs = {}
    for name in shlex.split(deps.read_text().replace('\\\n', ' ').split(':', 1)[1]):
        path = pathlib.Path(name).resolve()
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    def git(*args):
        return subprocess.check_output(['git', '-c', 'safe.directory=' + str(source), '-C', str(source), *args], text=True).strip()
    report['measurements'][label] = {'source': str(source), 'commit': git('rev-parse', 'HEAD'), 'tree': git('rev-parse', 'HEAD^{tree}'), 'status': git('status', '--porcelain'), 'command': command, 'nm_command': nm_command, 'directory': row['directory'], 'sizes': sizes, 'inputs_sha256': inputs}
    print(label, sizes, flush=True)
(a.output / 'layout.json').write_text(json.dumps(report, indent=2) + '\n')
