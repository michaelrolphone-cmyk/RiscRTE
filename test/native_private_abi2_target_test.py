#!/usr/bin/env python3
"""Small real-SDK private ABI objects/profile checks; never executes target code."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / 'lib/elf_loader/include/private'
PIN = 'fff9895c82d744c7237be8847347bdd1b07c6643'
PROFILE_SHA = 'c880590cd8c5ba64f091d5bef7b2499353755bca582f15533509c638e8efb544'


def profile():
    result = {}
    for line in (PRIVATE / 'privileged_native_cohort_v2.def').read_text().splitlines():
        if not line.startswith('RISC_NATIVE_COHORT_VALUE('):
            continue
        name, rest = line.removeprefix('RISC_NATIVE_COHORT_VALUE(').split(',', 1)
        _, value = rest.rsplit(',', 1)
        result[name] = int(value.strip().removesuffix(')').removesuffix('u'), 0)
    encoded = json.dumps(result, sort_keys=True, separators=(',', ':')).encode()
    assert len(result) == 33 and hashlib.sha256(encoded).hexdigest() == PROFILE_SHA
    return result


def command(entry, source, output):
    args = entry.get('arguments') or shlex.split(entry['command'])
    result = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if arg in ('-c', '-MD', '-MMD', '-MP') or arg == entry['file']:
            continue
        result.append(arg)
    return result + ['-include', str(PRIVATE/'esp_privileged_cohort_target.h'), '-c', str(source), '-o', str(output)]


def execute(args, cwd, log, success=True):
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True)
    log.write_text(result.stdout + result.stderr)
    assert (result.returncode == 0) == success, log.read_text()
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compile-commands', type=Path, required=True)
    ap.add_argument('--idf', type=Path, required=True)
    ap.add_argument('--qualification', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--firmware-elf', type=Path)
    args = ap.parse_args()
    assert subprocess.check_output(['git', '-C', args.idf, 'rev-parse', 'HEAD'], text=True).strip() == PIN
    entries = json.loads(args.compile_commands.read_text())
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    receipt = json.loads(args.qualification.read_text())
    expected_profile = profile()
    assert receipt['idf_commit'] == PIN and receipt['os_cpu_abi'] == 2
    assert receipt['compiler'] == '15.2.0' and receipt['compiler_distribution'] == 'esp-15.2.0_20251204'
    assert receipt['abi_profile'] == expected_profile and receipt['abi_profile_sha256'] == PROFILE_SHA
    assert receipt['native_cohort'] == 'esp32s3-idf6.1-gcc15.2.0-20251204-v1'
    inventory = set(re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)', (PRIVATE/'privileged_os_cpu_symbols_v2.def').read_text(), re.M))
    libc = set(re.findall(r'^RISC_PUBLIC_LIBC_COMPAT_SYMBOL\((\w+)\)', (PRIVATE/'privileged_public_libc_compat_v2.def').read_text(), re.M))
    assert len(inventory) == 36 and len(libc) == 5
    assert inventory | libc | {'printf','puts','putchar'} == set(receipt['imports'])
    report = {'sdk_commit': PIN, 'abi_profile_sha256': PROFILE_SHA, 'objects': {}, 'negative_profiles': [], 'hardware_verified': False}
    for name in ('esp_privileged_os_cpu', 'esp_privileged_elf', 'esp_privileged_imports', 'esp_privileged_manifest_imports', 'esp_elf_symbol'):
        matching = [e for e in entries if e['file'].endswith('/'+name+'.c')]
        assert len(matching) == 1
        entry = matching[0]
        flags = Path(entry['directory'])/'toolchain/cflags'
        assert '-mdisable-hardware-atomics' in flags.read_text()
        cmd = command(entry, Path(entry['file']), out/(name+'.o'))
        assert 'esp-15.2.0_20251204' in Path(cmd[0]).parts
        assert subprocess.check_output([cmd[0], '-dumpfullversion'], text=True).strip() == '15.2.0'
        execute(cmd, entry['directory'], out/(name+'.log'))
        (out/(name+'-command.json')).write_text(json.dumps(cmd, indent=2)+'\n')
        with (out/(name+'.o')).open('rb') as stream:
            elf = ELFFile(stream)
            assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_XTENSA'
            undefined = {s.name for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_shndx'] == 'SHN_UNDEF'}
            if name == 'esp_privileged_os_cpu':
                assert inventory <= undefined, inventory - undefined
            report['objects'][name] = {'sha256': hashlib.sha256((out/(name+'.o')).read_bytes()).hexdigest(), 'imports': sorted(undefined)}
    # Include headers first, then alter one ABI-relevant control. No amount of
    # unrelated valid configuration may allow the fixed native profile to drift.
    entry = next(e for e in entries if e['file'].endswith('/esp_privileged_os_cpu.c'))
    for macro, value in (('configNUMBER_OF_CORES',1), ('configTICK_RATE_HZ',100),
                         ('configTASK_NOTIFICATION_ARRAY_ENTRIES',2), ('CONFIG_LOG_VERSION',2),
                         ('CONFIG_FREERTOS_SMP',1), ('portBYTE_ALIGNMENT',8),
                         ('CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND',0)):
        source = out/(macro+'.c')
        source.write_text('#include "esp_idf_version.h"\n#include "esp_log.h"\n#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n#include "freertos/queue.h"\n'+
                          '#undef '+macro+'\n#define '+macro+' '+str(value)+'\n#include "private/esp_privileged_cohort_target.h"\n')
        cmd = command(entry, source, out/(macro+'.o'))
        index = cmd.index('-include'); del cmd[index:index+2]
        execute(cmd, entry['directory'], out/(macro+'.log'), success=False)
        report['negative_profiles'].append(macro)
    if args.firmware_elf:
        with args.firmware_elf.open('rb') as stream:
            elf = ELFFile(stream)
            symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
            for name in inventory:
                assert name in symbols and symbols[name]['st_shndx'] != 'SHN_UNDEF' and symbols[name]['st_value'], name
        report['resident_link_verified'] = str(args.firmware_elf)
    else:
        report['resident_link_verified'] = False
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Native private ABI2: 36 strong private links, five libc names, 33-field profile and seven rejected profile mutations PASS; no device execution')


if __name__ == '__main__':
    main()
