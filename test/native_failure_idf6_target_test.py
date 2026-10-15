#!/usr/bin/env python3
"""Compile only the recorder/sleep adapter and audit cache-safe object references.

Requires pyelftools and a configured native IDF build. This is not a substitute
for scripts/audit_failure_evidence.py on the final linked firmware.
"""
import argparse
import json
import re
import shlex
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parents[1]
PIN = 'fff9895c82d744c7237be8847347bdd1b07c6643'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compile-commands', type=Path, required=True)
    p.add_argument('--idf', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    idf = a.idf.resolve()
    assert subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip() == PIN
    commands = json.loads(a.compile_commands.read_text())
    def entry(suffix):
        found = [e for e in commands if e['file'].endswith(suffix)]
        assert len(found) == 1, suffix
        return found[0]
    recorder = entry('/NativeFailureEvidence.cpp')
    def compile_one(e, name, extra=(), source=None, failure=None):
        cmd = shlex.split(e['command'])
        cmd[cmd.index('-o') + 1] = str(out / (name + '.o'))
        for flag in ('-MF', '-MT'):
            if flag in cmd:
                cmd[cmd.index(flag) + 1] = str(out / (name + ('.d' if flag == '-MF' else '.o')))
        if source:
            cmd[cmd.index('-c') + 1] = str(source)
        cmd += list(extra)
        result = subprocess.run(cmd, cwd=e['directory'], text=True, capture_output=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        (out / (name + '-command.json')).write_text(json.dumps(cmd, indent=2) + '\n')
        if failure:
            assert result.returncode != 0 and failure in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr
        return out / (name + '.o')

    config = out / 'capture-config.h'
    config.write_text('#include "sdkconfig.h"\n#undef CONFIG_ESP_PANIC_HANDLER_IRAM\n'
                      '#define CONFIG_ESP_PANIC_HANDLER_IRAM 1\n'
                      '#undef CONFIG_ESP_SYSTEM_IN_IRAM\n#define CONFIG_ESP_SYSTEM_IN_IRAM 1\n')
    flags = ['-DRISC_NATIVE_FAILURE_EVIDENCE=1', '-include', str(config), '-fstack-usage']
    obj = compile_one(recorder, 'recorder', flags)
    for missing in ('CONFIG_ESP_SYSTEM_IN_IRAM', 'CONFIG_ESP_PANIC_HANDLER_IRAM'):
        bad = out / (missing + '.h')
        bad.write_text('#include "' + str(config) + '"\n#undef ' + missing + '\n#define ' + missing + ' 0\n')
        compile_one(recorder, missing, ['-DRISC_NATIVE_FAILURE_EVIDENCE=1', '-include', str(bad)],
                    failure='Native failure capture requires ESP_SYSTEM_IN_IRAM and ESP_PANIC_HANDLER_IRAM')
    bad = out / 'wrong-version.h'
    bad.write_text('#include "' + str(config) + '"\n#include <esp_idf_version.h>\n'
                   '#undef ESP_IDF_VERSION\n#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,1)\n')
    compile_one(recorder, 'wrong-version', ['-DRISC_NATIVE_FAILURE_EVIDENCE=1', '-include', str(bad)],
                failure='Native failure capture requires the audited ESP32-S3')
    sleep = out / 'sleep.cpp'
    sleep.write_text('#include "ports/esp32s3/NativeSleep.h"\n'
        'extern "C" bool check_sleep(unsigned char p) { using namespace RiscCpu::NativeSleep; '
        'return stackReady() && valid(p) && arm(p,true,false) && clear(p,false) && '
        'setArm(1ull<<p,1ull<<p,0) && setClear(1ull<<p,1ull<<p,0); }\n')
    compile_one(recorder, 'sleep', source=sleep)

    # These SDK callees changed headers/placement in IDF6. Compile the actual
    # pinned translation units, never synthetic declarations or substitute code.
    sdk_sources = ('/esp_system/port/arch/xtensa/debug_helpers.c',
                   '/esp_system/port/soc/esp32s3/reset_reason.c',
                   '/esp_hw_support/esp_memory_utils.c',
                   '/esp_psram/system_layer/esp_psram.c')
    objects = [obj]
    for source in sdk_sources:
        objects.append(compile_one(entry(source), Path(source).stem, ['-include', str(config)]))
    # SDK linker fragments place these complete files/functions in noflash.
    hw_link = (idf / 'components/esp_hw_support/linker.lf').read_text()
    sys_link = (idf / 'components/esp_system/linker.lf').read_text()
    assert 'esp_memory_utils (noflash)' in hw_link
    assert 'reset_reason:esp_reset_reason_get_hint (noflash)' in sys_link
    loaded = []
    streams = []
    for path in objects:
        stream = path.open('rb'); streams.append(stream)
        elf = ELFFile(stream); syms = elf.get_section_by_name('.symtab')
        loaded.append((path, elf, syms))
    globals_ = {}
    for i, (_, elf, syms) in enumerate(loaded):
        for symbol in syms.iter_symbols():
            if isinstance(symbol['st_shndx'], int) and symbol.name:
                globals_.setdefault(symbol.name, (i, symbol))
    seen = set(); edges = []; todo = []
    def add_symbol(i, symbol):
        section = loaded[i][1].get_section(symbol['st_shndx'])
        todo.append((i, section.name))
        # GCC's Xtensa literal section may precede the executable section.
        literal = section.name + '.literal' if section.name.startswith('.iram') else '.literal.' + symbol.name
        if loaded[i][1].get_section_by_name(literal): todo.append((i, literal))
    roots = []
    for symbol in loaded[0][2].iter_symbols():
        if symbol.name == '__wrap_esp_panic_handler' or re.search(r'6retainE[il]PKc$', symbol.name):
            assert loaded[0][1].get_section(symbol['st_shndx']).name.startswith('.iram')
            roots.append(symbol.name)
            add_symbol(0, symbol)
    assert len(roots) == 2, roots
    while todo:
        i, name = todo.pop()
        if (i, name) in seen: continue
        seen.add((i, name)); path, elf, syms = loaded[i]
        section = elf.get_section_by_name(name)
        assert section is not None
        if not name.startswith('.iram'):
            assert path.stem in ('esp_memory_utils', 'reset_reason'), (path.name, name)
            if path.stem == 'reset_reason': assert 'esp_reset_reason_get_hint' in name
        for relocs in elf.iter_sections():
            if not isinstance(relocs, RelocationSection) or elf.get_section(relocs['sh_info']).name != name: continue
            for relocation in relocs.iter_relocations():
                symbol = syms.get_symbol(relocation['r_info_sym'])
                target = symbol.name
                if target == '__real_esp_panic_handler': continue
                assert not re.search(r'memset|memcpy|malloc|calloc|realloc|printf|__stack_chk', target), target
                if symbol['st_shndx'] == 'SHN_UNDEF':
                    if target == 'g_panic_abort': continue
                    assert target in globals_, target
                    j, symbol = globals_[target]
                    edges.append([name, target]); add_symbol(j, symbol)
                elif isinstance(symbol['st_shndx'], int):
                    destination = elf.get_section(symbol['st_shndx']).name
                    if destination.startswith(('.dram', '.bss', '.data', '.rtc_noinit')): continue
                    assert destination.startswith(('.iram', '.text', '.literal')), (path.name, name, destination)
                    # Plain SDK text/literals are allowed only where explicitly
                    # assigned noflash by the audited SDK linker fragment.
                    if not destination.startswith('.iram'):
                        assert path.stem in ('esp_memory_utils', 'reset_reason'), (path.name, destination)
                        if path.stem == 'reset_reason': assert 'esp_reset_reason_get_hint' in destination
                    todo.append((i, destination))
    usage = (out / 'recorder.su').read_text().splitlines()
    frames = [line for line in usage if '__wrap_esp_panic_handler' in line or '::retain(' in line]
    assert len(frames) == 2 and all(int(x.split('\t')[1]) <= 256 and x.endswith('\tstatic') for x in frames)
    report = {'sdk_commit': PIN, 'roots': roots, 'object_closure': sorted([loaded[i][0].name, n] for i, n in seen),
              'sdk_call_edges': edges, 'stack_frames': frames, 'negative_guards': 'PASS',
              'linked_firmware_verified': False, 'hardware_verified': False}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('IDF6 failure/sleep affected objects, ABI assertions, IRAM closure and negative guards PASS')


if __name__ == '__main__': main()
