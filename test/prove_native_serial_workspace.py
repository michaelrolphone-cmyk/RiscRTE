#!/usr/bin/env python3
"""Check real serial workspace symbols and internal gates in a host or target ELF."""
import argparse
import subprocess

WORKSPACES = {
    'endpoint': ('NativeSerialFiles', 'workspaceStorageE'),
    'provider': ('NativeSerialStorage', 'privateProviderStorageE'),
}


def prove(elf, selected, external, objdump='objdump'):
    symbols = []
    for line in subprocess.check_output([objdump, '-t', str(elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 6 and parts[2] == 'O':
            symbols.append((parts[5], parts[3], int(parts[0], 16), int(parts[4], 16)))
    for selection in selected:
        namespace, suffix = WORKSPACES[selection]
        matches = [s for s in symbols if namespace in s[0] and s[0].endswith(suffix)]
        assert len(matches) == 1, (selection, matches)
        name, section, address, size = matches[0]
        assert size >= (2500 if selection == 'endpoint' else 700), (name, size)
        assert address % 8 == 0, (name, address)
        assert section.startswith('.ext_ram.bss') == external, (name, section, external)
        if not external:
            assert section.startswith('.bss'), (name, section)
        controls = [s for s in symbols if namespace in s[0] and s[0] != name]
        assert controls, selection
        for control in controls:
            assert not control[1].startswith('.ext_ram'), control
        print(f'Serial {selection}: {size} raw bytes in {section}; control objects remain internal PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--workspace', choices=WORKSPACES, action='append', required=True)
    parser.add_argument('--external', choices=(0, 1), type=int, required=True)
    parser.add_argument('--objdump', default='objdump')
    args = parser.parse_args()
    prove(args.elf, args.workspace, bool(args.external), args.objdump)
