#!/usr/bin/env python3
"""Exact linked capacity evidence; no graph admission, product or device action."""
import argparse
import hashlib
import io
import json
import re
from pathlib import Path
from elftools.elf.elffile import ELFFile

PREFIX = b'RISC_RUNTIME_CAPACITY:'
SYMBOL = 'risc_runtime_capacity'
LIMITS = {17: (19, 17, 32), 26: (24, 26, 42), 29: (24, 29, 45)}

def require(ok, message):
    if not ok:
        raise ValueError(message)

def prove(blobs, providers):
    require(type(providers) is int and providers in LIMITS, 'unsupported provider capacity')
    apps, providers, grants = LIMITS[providers]
    marker = PREFIX + f'{apps}:{providers}:{grants}'.encode() + b'\0'
    binary = blobs.get('firmware.bin', b'')
    require(binary.count(PREFIX) == 1 and binary.count(marker) == 1,
            'missing, conflicting or duplicate runtime capacity marker: firmware.bin')
    require(bool(blobs.get('firmware.elf')), 'capacity proof requires ELF bytes')
    image = ELFFile(io.BytesIO(blobs['firmware.elf']))
    require(image.elfclass == 32 and image.little_endian and image['e_machine'] == 'EM_XTENSA',
            'capacity proof requires ELF32 LE Xtensa')
    # GCC may repeat a const initializer in DWARF. Only allocated file bytes
    # can establish or conflict with this linked capability.
    allocated = [section.data() for section in image.iter_sections()
                 if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS']
    require(sum(data.count(PREFIX) for data in allocated) == 1
            and sum(data.count(marker) for data in allocated) == 1,
            'missing, conflicting or duplicate allocated runtime capacity marker')
    table = image.get_section_by_name('.symtab')
    require(table is not None, 'capacity proof requires linked symbols')
    matches = table.get_symbol_by_name(SYMBOL) or []
    require(len(matches) == 1, 'missing or duplicate runtime capacity symbol')
    symbol = matches[0]
    require(symbol['st_info']['bind'] == 'STB_GLOBAL' and symbol['st_info']['type'] == 'STT_OBJECT'
            and type(symbol['st_shndx']) is int and symbol['st_size'] == len(marker),
            'invalid linked runtime capacity symbol')
    section = image.get_section(symbol['st_shndx'])
    # The pinned ESP32-S3 linker labels .flash.rodata SHF_WRITE despite its
    # read-only physical DROM mapping. Check that mapping instead of its flag.
    require(section.name == '.flash.rodata' and section['sh_flags'] & 2
            and not section['sh_flags'] & 4 and section['sh_type'] == 'SHT_PROGBITS'
            and 0x3C000000 <= symbol['st_value']
            and symbol['st_value'] + len(marker) <= 0x3E000000,
            'capacity marker must be allocated ESP32-S3 flash rodata')
    offset = symbol['st_value'] - section['sh_addr']
    require(0 <= offset and offset + len(marker) <= section['sh_size']
            and section.data()[offset:offset + len(marker)] == marker,
            'linked capacity symbol differs from expected bounds')
    require(any(segment['p_type'] == 'PT_LOAD'
                and segment['p_vaddr'] <= symbol['st_value']
                and symbol['st_value'] + len(marker) <= segment['p_vaddr'] + segment['p_filesz']
                and segment['p_offset'] + symbol['st_value'] - segment['p_vaddr'] == section['sh_offset'] + offset
                for segment in image.iter_segments()), 'capacity symbol is outside loadable file bytes')
    return {'schema': 1, 'apps': apps, 'providers': providers, 'graph_grants': grants,
            'shared_nonboot_grants': grants - providers, 'live_grants_per_invocation': 16,
            'marker': marker[:-1].decode(), 'symbol': SYMBOL,
            'elf_sha256': hashlib.sha256(blobs['firmware.elf']).hexdigest(),
            'firmware_sha256': hashlib.sha256(blobs['firmware.bin']).hexdigest()}

def verify_candidate(blobs, record):
    """Recompute current evidence while preserving frozen pre-0.2.4 inputs."""
    declared = record['native_proof'].get('runtime_capacity')
    if declared is None:
        require(all(PREFIX not in blobs.get(name, b'') for name in ('firmware.elf', 'firmware.bin')),
                'candidate runtime capacity proof missing')
        version = record.get('firmware_version', '')
        require(isinstance(version, str) and re.fullmatch(r'\d+\.\d+\.\d+', version)
                and tuple(map(int, version.split('.'))) < (0, 2, 4),
                'Runtime 0.2.4 and later require linked capacity proof')
        return None
    require(isinstance(declared, dict), 'candidate runtime capacity proof must be an object')
    providers = declared.get('providers')
    require(type(providers) is int and providers in (26, 29), 'paired candidate provider capacity')
    actual = prove(blobs, providers)
    require(json.dumps(actual, sort_keys=True, separators=(',', ':')) ==
            json.dumps(declared, sort_keys=True, separators=(',', ':')),
            'candidate runtime capacity proof mismatch')
    return actual

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('--providers', type=int, choices=tuple(LIMITS), required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = prove({'firmware.elf': args.elf.read_bytes(), 'firmware.bin': args.firmware.read_bytes()}, args.providers)
    encoded = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end='')
