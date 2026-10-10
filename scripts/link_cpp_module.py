"""Link a PIC C++17 application using the shared, owner-scoped ELF SDK.

The Xtensa toolchain's non-PIC libstdc++ archive is deliberately not linked.
Compile TemplateConfig.h, RiscCppRuntime.cpp and the pinned upstream support
translation units into the application's own object set first.
"""
import hashlib
import json
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]


def mapped_relocations(path):
    with Path(path).open('rb') as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_XTENSA':
            raise ValueError('Expected Xtensa ELF32')
        sections = list(elf.iter_sections())
        mapped = [s for s in sections if s.name in ('.text', '.data', '.rodata', '.data.rel.ro', '.bss')]
        count = 0
        for section in sections:
            if section['sh_type'] != 'SHT_RELA':
                continue
            for relocation in section.iter_relocations():
                kind, offset = relocation['r_info_type'], relocation['r_offset']
                if not kind:
                    if relocation['r_info'] or offset or relocation['r_addend']:
                        raise ValueError('Noncanonical relocation padding')
                    continue
                owner = next((s for s in mapped if s['sh_addr'] <= offset and offset + 4 <= s['sh_addr'] + s['sh_size']), None)
                if kind not in (2, 3, 4, 5) or offset % 4 or owner is None:
                    raise ValueError(f'Unsupported relocation write at {offset:#x}')
                if kind == 5:
                    value = struct.unpack_from('<I', owner.data(), offset - owner['sh_addr'])[0]
                    if value and not any(s['sh_addr'] <= value < s['sh_addr'] + s['sh_size'] for s in mapped):
                        raise ValueError(f'Unmapped relative pointer {value:#x} at {offset:#x}')
                count += 1
        return count


def link(compiler, objects, output, exports):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if any(not name.isascii() or not name.isidentifier() for name in exports):
        raise ValueError('Invalid export name')
    mapping = output.with_suffix('.exports.map')
    mapping.write_text('{ global: ' + '; '.join(sorted(exports)) + '; local: *; };\n')
    command = [str(compiler), '-nostdlib', '-nostartfiles', '-shared',
               '-Wl,--no-relax', '-Wl,--hash-style=sysv', '-Wl,--gc-sections',
               '-Wl,--version-script=' + str(mapping),
               '-Wl,-T,' + str(ROOT / 'sdk/cxx/module.ld'),
               *map(str, objects), '-lgcc', '-lm', '-o', str(output)]
    subprocess.run(command, check=True)
    count = mapped_relocations(output)
    symbols = subprocess.check_output([str(compiler).removesuffix('g++') + 'nm', '-D', str(output)], text=True)
    imports = sorted(line.split()[-1] for line in symbols.splitlines() if ' U ' in ' ' + line)
    actual = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) == 3 and line.split()[1] in ('T', 'R', 'D', 'B')}
    if actual != set(exports):
        raise ValueError('Unexpected module exports: ' + repr(actual))
    return {'bytes': output.stat().st_size, 'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
            'imports': imports, 'exports': sorted(actual), 'mapped_relocations': count}
