#!/usr/bin/env python3
"""Measure linked Xtensa layouts from DWARF and actual static symbols."""
import argparse
import hashlib
import io
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile
from runtime_capacity_proof import prove, require

TYPES = {'RiscBoot::Runtime': 'runtime_bytes', 'RuntimeProviders::GraphV2': 'graph_bytes',
         'RiscCpu::Port': 'cpu_port_bytes', 'RuntimeProviders::OwnedNodeV2': 'owned_node_bytes'}

def measure(elf_bytes, firmware_bytes, providers):
    capacity = prove({'firmware.elf': elf_bytes, 'firmware.bin': firmware_bytes}, providers)
    elf = ELFFile(io.BytesIO(elf_bytes))
    require(elf.has_dwarf_info(), 'target layout proof requires DWARF')
    found = {key: set() for key in TYPES}
    bool_sizes = set()
    for cu in elf.get_dwarf_info().iter_CUs():
        for die in cu.iter_DIEs():
            attrs = die.attributes
            if 'DW_AT_name' not in attrs or 'DW_AT_byte_size' not in attrs:
                continue
            name = attrs['DW_AT_name'].value.decode()
            size = attrs['DW_AT_byte_size'].value
            if die.tag == 'DW_TAG_base_type' and name == 'bool':
                bool_sizes.add(size)
            if die.tag not in ('DW_TAG_structure_type', 'DW_TAG_class_type'):
                continue
            parent = die.get_parent()
            while parent is not None:
                if parent.tag == 'DW_TAG_namespace' and 'DW_AT_name' in parent.attributes:
                    name = parent.attributes['DW_AT_name'].value.decode() + '::' + name
                parent = parent.get_parent()
            if name in found:
                found[name].add(size)
    require(all(len(sizes) == 1 for sizes in found.values()) and bool_sizes == {1},
            'missing or inconsistent target layout types')
    sizes = {TYPES[key]: next(iter(values)) for key, values in found.items()}
    symbols = {symbol.name: symbol for symbol in elf.get_section_by_name('.symtab').iter_symbols()}
    cpu = symbols.get('_ZN12_GLOBAL__N_13cpuE')
    registry = symbols.get('_ZN14RuntimeStreams12_GLOBAL__N_18registryE')
    require(cpu is not None and cpu['st_size'] == sizes['cpu_port_bytes'] and registry is not None,
            'missing or inconsistent linked static layout')
    sizes.update(stream_registry_bytes=registry['st_size'], graph_matrix_bytes=providers * providers,
                 static_dram_data_bytes=elf.get_section_by_name('.dram0.data')['sh_size'],
                 static_dram_bss_bytes=elf.get_section_by_name('.dram0.bss')['sh_size'])
    return {'schema': 1, 'capacity': capacity, 'pointer_bytes': elf.elfclass // 8, **sizes,
            'firmware_bytes': len(firmware_bytes), 'elf_sha256': hashlib.sha256(elf_bytes).hexdigest(),
            'scope': 'Linked object layouts and static sections only. Graph is included in Runtime. '
                     'Excludes dynamically allocated provider snapshots/code, stack and heap peaks; no hardware claim.'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('--providers', type=int, choices=(17, 26, 29), required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(measure(args.elf.read_bytes(), args.firmware.read_bytes(), args.providers), indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')
