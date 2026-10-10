#!/usr/bin/env python3
"""Generate/check a build-owned policy for the exact frozen Reader controller.

No manifest, sidecar, boot JSON or package data can select this header. A trusted
build must explicitly include it using RISC_NATIVE_PROVIDER_POLICY_HEADER.
Generating the file neither selects it nor establishes board/hardware admission.
"""
import argparse
import hashlib
import io
import re
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from audit_provider_diagnostics import address_bytes, symbols, text_at

PINNED_SHA256 = 'd0864938e573ac933fefc106991cfb033ec9599cb09902a518c53ffcaf57926c'
DRIVER_ID = 'usb-controller-esp32s3'
VERSION = '0.1.24'
CAPABILITY = 'usb.controller'
REQUIREMENTS = [('board.power.vbus', 1), ('platform.usb.phy.resource', 1)]


def relative_path(value):
    if len(value.encode('utf-8')) > 192 or not re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', value):
        raise ValueError('The selected ELF path must be a bounded relative native path')
    if any(part in ('.', '..') for part in value.split('/')) or not value.endswith('.elf'):
        raise ValueError('The selected ELF path must name a relative .elf without traversal')
    basename = value.rsplit('/', 1)[-1]
    if len(basename) > 127 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.elf', basename) or '..' in basename:
        raise ValueError('The selected basename must satisfy the native executable identity contract')
    return value


def inspect_controller(data):
    if hashlib.sha256(data).hexdigest() != PINNED_SHA256:
        raise ValueError('Controller differs from the pinned 0.1.24 image; no policy generated')
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_XTENSA' or elf['e_type'] != 'ET_DYN':
        raise ValueError('Controller must be the pinned ELF32/Xtensa shared image')
    tables = [elf.get_section_by_name(name) for name in ('.dynsym', '.symtab')]
    if any(table is None for table in tables):
        raise ValueError('Both controller symbol tables are required')
    imports = sorted({symbol.name for table in tables for symbol in table.iter_symbols()
                      if symbol.name and symbol['st_shndx'] == 'SHN_UNDEF'})
    if len(imports) != 47 or not {'printf', 'puts', 'putchar'}.issubset(imports):
        raise ValueError('Unexpected controller import inventory')
    if any(not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name) or len(name) > 127 for name in imports):
        raise ValueError('Unsupported native import spelling')
    exports = [symbol for symbol in tables[0].iter_symbols()
               if symbol.name == 't5_driver_get' and symbol['st_shndx'] != 'SHN_UNDEF'
               and symbol['st_info']['bind'] == 'STB_GLOBAL' and symbol['st_info']['type'] == 'STT_FUNC']
    if len(exports) != 1:
        raise ValueError('One dynamic global provider entry is required')
    entries = symbols(elf)
    descriptor = entries.get('_ZN12_GLOBAL__N_1L10hid_driverE')
    if descriptor is None or descriptor['st_size'] != 40:
        raise ValueError('Pinned controller descriptor is absent or has changed')
    abi, size, driver, capability, api = struct.unpack('<5I', address_bytes(elf, descriptor['st_value'], 20))
    if (abi, size, text_at(elf, driver), text_at(elf, capability), api) != (2, 40, DRIVER_ID, CAPABILITY, 1):
        raise ValueError('Pinned controller descriptor identity does not match the trusted policy')
    return imports


def render(data, selected_path):
    selected_path = relative_path(selected_path)
    imports = inspect_controller(data)
    names = '\n'.join(f'    "{name}",' for name in imports)
    digest = ','.join('0x' + PINNED_SHA256[i:i + 2] for i in range(0, 64, 2))
    return f'''#pragma once
// Generated from frozen Reader {DRIVER_ID}@{VERSION}; SHA-256 {PINNED_SHA256}.
// Trusted build input only. No sidecar/manifest/boot JSON may select this file.
// Generation is not controller activation or physical qualification.
#include "runtime/drivers/NativeProviderPolicyV1.h"
namespace RuntimeProviders {{
inline NativeProviderPolicySetV1 selectedNativeProviderPoliciesV1() {{
  static const char* const imports[]={{
{names}
  }};
  static const NativeProviderRequirementV1 requirements[]={{
    {{"board.power.vbus",1}},{{"platform.usb.phy.resource",1}}
  }};
  static const NativeProviderPolicyV1 policy{{
    "{selected_path}","{DRIVER_ID}","{VERSION}","{CAPABILITY}",1,1,{len(data)},
    {{{digest}}},imports,47,requirements,2,1
  }};
  return {{&policy,1}};
}}
}} // namespace RuntimeProviders
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controller', type=Path, required=True)
    parser.add_argument('--relative-elf-path', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output', type=Path)
    mode.add_argument('--check', type=Path)
    args = parser.parse_args()
    try:
        expected = render(args.controller.read_bytes(), args.relative_elf_path)
        if args.check:
            if args.check.read_bytes() != expected.encode('utf-8'):
                raise ValueError('Generated policy differs from the exact trusted policy')
            print('Exact frozen-controller policy check PASS')
        else:
            args.output.write_bytes(expected.encode('utf-8'))
            print('Generated exact 47-import trusted policy; no build selection or activation')
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
