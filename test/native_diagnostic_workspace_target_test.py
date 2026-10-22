#!/usr/bin/env python3
"""Compile the production diagnostic adapter and prove target storage placement.

This is an object/layout test, not the mandatory final-ELF IQ proof or hardware
qualification. Every command writes to --output, never the firmware build tree.
"""
import argparse
import hashlib
import json
import shlex
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

PIN = 'fff9895c82d744c7237be8847347bdd1b07c6643'



def verify_workspace_layout(sizes, storage_bytes, diagnostics, performance, capacity):
    assert len(sizes) == 6
    assert storage_bytes == sizes[0] == sum(sizes[2:]), (storage_bytes, sizes)
    assert sizes[1] > 0 and sizes[0] % sizes[1] == 0
    assert sizes[4] == (96 if diagnostics else 0), sizes
    if not diagnostics:
        assert sizes[2:5] == (0, 0, 0)
    if not performance:
        assert sizes[5] == 0
    if diagnostics and performance:
        # Current production carries one 96-byte STAT copy inside Replay and
        # another in ReplayWorkspace. Preserve the prior replay-size bounds
        # and account explicitly for both already-existing source members.
        assert sizes[0] == (7160 if capacity == 64 else 9208) + 2 * 96, sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands', type=Path, required=True)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    idf = args.idf.resolve()
    assert subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip() == PIN
    entries = [e for e in json.loads(args.compile_commands.read_text()) if e['file'].endswith('/SleepDiagnostics.cpp')]
    assert len(entries) == 1
    entry = entries[0]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'diagnostic-workspace.cpp'
    source.write_text('''#include <esp_idf_version.h>
#include "ports/esp32s3/SleepDiagnostics.cpp"
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6,1,0));
#if !RISC_NATIVE_ESP_IDF || !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY || !CONFIG_SPIRAM_BOOT_INIT
#error Native workspace proof requires selected S3 PSRAM boot and external BSS
#endif
extern const uint32_t diagnostic_workspace_sizes[] = {
#if RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE
  sizeof(RiscDiagnostics::ReplayWorkspace), alignof(RiscDiagnostics::ReplayWorkspace),
#else
  0, 0,
#endif
#if RISC_SLEEP_DIAGNOSTICS
  sizeof(RiscDiagnostics::Replay), sizeof(RiscDiagnostics::Checkpoint),
  sizeof(RiscDiagnostics::SerialStatSnapshot),
#else
  0, 0, 0,
#endif
#if RISC_PERFORMANCE_TRACE
  sizeof(RiscPerf::Replay),
#else
  0,
#endif
};
''')
    report = {'sdk_commit': PIN, 'source_sha256': hashlib.sha256(Path(entry['file']).read_bytes()).hexdigest(),
              'objects': {}, 'linked_firmware_verified': False, 'hardware_verified': False}
    for diagnostics, performance, capacity in ((1, 1, 64), (1, 1, 128), (1, 0, 64), (0, 1, 64), (0, 0, 64)):
        name = f'diagnostic-workspace-{diagnostics}-{performance}-{capacity}'
        command = list(entry.get('arguments') or shlex.split(entry['command']))
        obj = out / (name + '.o')
        command[command.index('-o') + 1] = str(obj)
        command[command.index('-c') + 1] = str(source)
        for flag, extension in (('-MF', '.d'), ('-MT', '.o')):
            if flag in command:
                command[command.index(flag) + 1] = str(out / (name + extension))
        for key, value in (('RISC_SLEEP_DIAGNOSTICS', diagnostics), ('RISC_PERFORMANCE_TRACE', performance),
                           ('RISC_PERFORMANCE_TRACE_CAPACITY', capacity)):
            command += ['-U' + key, f'-D{key}={value}']
        result = subprocess.run(command, cwd=entry['directory'], text=True, capture_output=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        (out / (name + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
        assert result.returncode == 0, result.stdout + result.stderr
        with obj.open('rb') as stream:
            elf = ELFFile(stream)
            assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_XTENSA'
            symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
            objects = [s for s in symbols if s['st_info']['type'] == 'STT_OBJECT']
            def named(name):
                found = [s for s in objects if s.name.endswith(str(len(name)) + name + 'E') or s.name == name]
                assert len(found) == 1, (name, [s.name for s in found])
                return found[0]
            sizes_symbol = named('diagnostic_workspace_sizes')
            size_section = elf.get_section(sizes_symbol['st_shndx'])
            sizes = struct.unpack('<6I', size_section.data()[sizes_symbol['st_value']:sizes_symbol['st_value'] + sizes_symbol['st_size']])
            workspace = [s for s in objects if s.name.endswith('13replayStorageE')]
            if diagnostics or performance:
                assert len(workspace) == 1
                storage = workspace[0]
                section = elf.get_section(storage['st_shndx'])
                assert section.name.startswith('.ext_ram.bss.')
                verify_workspace_layout(sizes, storage['st_size'], diagnostics, performance, capacity)
                assert section['sh_addralign'] >= sizes[1]
                assert section['sh_type'] == 'SHT_NOBITS' or not any(section.data())
            else:
                assert not workspace and sizes == (0, 0, 0, 0, 0, 0)
            # Synchronization/owner state, USB recovery and fencing remain internal.
            controls = {}
            for name in ('owner', 'outputting', 'unfinishedLine', 'recovery', 'usbFenced', 'draining'):
                if name == 'draining' and not any(s.name.endswith('8drainingE') for s in objects):
                    continue  # Selected products may omit the native observer.
                symbol = named(name)
                section = elf.get_section(symbol['st_shndx'])
                assert section.name.startswith(('.bss.', '.data.', '.dram1.')), (name, section.name)
                controls[name] = {'section': section.name, 'bytes': symbol['st_size']}
            if diagnostics:
                journal = named('journal')
                assert journal['st_size'] == 1288 and elf.get_section(journal['st_shndx']).name.startswith('.rtc_noinit.')
            imports = {s.name for s in symbols if s['st_shndx'] == 'SHN_UNDEF'}
            assert not any('heap_caps_' in name or name in ('malloc', 'calloc', 'realloc', 'free', '_Znwj', '_Znaj') for name in imports)
            assert not any(section.name.startswith('.init_array') and section['sh_size'] for section in elf.iter_sections())
            report['objects'][obj.name] = {'workspace_bytes': sizes[0], 'alignment': sizes[1],
                'sleep_replay_bytes': sizes[2], 'checkpoint_bytes': sizes[3], 'stat_snapshot_bytes': sizes[4], 'performance_replay_bytes': sizes[5],
                'internal_controls': controls, 'allocator_imports': False, 'global_constructor': False}
    disabled_boot = out / 'disable-psram-boot.h'
    disabled_boot.write_text('#include <sdkconfig.h>\n#undef CONFIG_SPIRAM_BOOT_INIT\n#define CONFIG_SPIRAM_BOOT_INIT 0\n')
    invalid = list(command) + ['-URISC_SLEEP_DIAGNOSTICS', '-DRISC_SLEEP_DIAGNOSTICS=1',
                               '-include', str(disabled_boot)]
    invalid[invalid.index('-o') + 1] = str(out / 'invalid-boot.o')
    result = subprocess.run(invalid, cwd=entry['directory'], text=True, capture_output=True)
    (out / 'invalid-boot.log').write_text(result.stdout + result.stderr)
    assert result.returncode != 0 and 'External diagnostic workspace requires SDK PSRAM boot initialization' in result.stderr
    report['missing_psram_boot_rejected'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print('Native diagnostic workspace: exact SDK6.1 Xtensa layout, zero-only external BSS, 64/128 capacities, RTC journal, internal controls and allocation-free startup PASS (no final-link or hardware claim)')


if __name__ == '__main__':
    main()
