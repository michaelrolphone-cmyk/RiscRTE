#!/usr/bin/env python3
"""Offline native-IDF6 trust-boundary tests and opt-in exact-artifact checks."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_usb_controller_idf61_policy as policy


def qualification():
    config_hash, commands_hash = '1' * 64, '2' * 64
    return {
        'schema_version': 1, 'status': 'experimental-native-candidate-not-hardware-qualified',
        'driver_id': policy.DRIVER_ID, 'version': policy.VERSION, 'file_name': 'driver.elf',
        'architecture': 'xtensa-esp32s3', 'target': 'esp32s3',
        'provider_abi': 2, 'os_cpu_abi': 2, 'native_cohort': policy.NATIVE_COHORT,
        'idf_commit': policy.IDF_COMMIT, 'usb_commit': policy.USB_COMMIT,
        'usb_version': '1.6.0', 'compiler': '15.2.0',
        'compiler_distribution': 'esp-15.2.0_20251204',
        'optimization': '-Os', 'abi_profile': dict(policy.ABI_PROFILE),
        'abi_profile_sha256': policy.ABI_PROFILE_SHA256,
        'requires': copy.deepcopy(policy.REQUIREMENTS),
        'provides': [{'capability': policy.CAPABILITY, 'api': 1}],
        'size': policy.PINNED_SIZE, 'sha256': policy.PINNED_SHA256,
        'imports': list(policy.IMPORTS), 'usb_config': dict(policy.USB_CONFIG),
        'sdkconfig_sha256': config_hash, 'compile_commands_sha256': commands_hash,
        'source_hashes': {
            '/checkout/Drivers/usb_controller_esp32s3/idf61-imports.txt': policy.IMPORT_FILE_SHA256,
            '/checkout/build/config/sdkconfig.h': config_hash,
            '/checkout/build/compile_commands.json': commands_hash,
        },
        'loader_map': copy.deepcopy(policy.LOADER_MAP),
    }


def elf_fixture(imports=policy.IMPORTS, descriptor=None, machine=94, elf_type=3,
                export_name='t5_driver_get', export_info=0x12, descriptor_size=40,
                omitted_section=None):
    """Small real ELF32 fixture for parser branches; never accepted by the pin."""
    descriptor_name = '_ZN12_GLOBAL__N_1L10hid_driverE'
    names = ['', '.shstrtab', '.dynstr', '.dynsym', '.strtab', '.symtab',
             '.text', '.rodata', '.data.rel.ro']
    shstrings = b'\0'
    offsets = {}
    for name in names[1:]:
        offsets[name] = len(shstrings)
        shstrings += (name if name != omitted_section else '.absent').encode() + b'\0'
    strings = b'\0'
    symbol_offsets = {}
    for name in (*imports, export_name, descriptor_name):
        symbol_offsets[name] = len(strings)
        strings += name.encode() + b'\0'
    symbol = lambda name, value, size, info, section: struct.pack(
        '<IIIBBH', symbol_offsets[name], value, size, info, 0, section)
    dynamic = bytes(16) + b''.join(symbol(name, 0, 0, 0x10, 0) for name in imports)
    dynamic += symbol(export_name, 0x1000, 4, export_info, 6)
    symtab = dynamic + symbol(descriptor_name, 0x3000, descriptor_size, 0x01, 8)
    driver = (policy.DRIVER_ID + '\0').encode()
    rodata = driver + (policy.CAPABILITY + '\0').encode()
    fields = descriptor or (2, 40, 0x2000, 0x2000 + len(driver), 1, 0x3028,
                            0x1004, 0x1008, 0x100c, 0x1010)
    data = struct.pack('<10I', *fields) + struct.pack('<II', 1, 52)
    # name, type, flags, address, contents, linked-section, info, entry-size
    sections = [('.shstrtab', 3, 0, 0, shstrings, 0, 0, 0),
                ('.dynstr', 3, 0, 0, strings, 0, 0, 0),
                ('.dynsym', 11, 0, 0, dynamic, 2, 1, 16),
                ('.strtab', 3, 0, 0, strings, 0, 0, 0),
                ('.symtab', 2, 0, 0, symtab, 4, 1, 16),
                ('.text', 1, 6, 0x1000, bytes(32), 0, 0, 0),
                ('.rodata', 1, 2, 0x2000, rodata, 0, 0, 0),
                ('.data.rel.ro', 1, 3, 0x3000, data, 0, 0, 0)]
    image = bytearray(52)
    headers = [bytes(40)]
    for name, kind, flags, address, contents, link, info, entries in sections:
        image += bytes((-len(image)) % 4)
        headers.append(struct.pack('<10I', offsets[name], kind, flags, address,
                                   len(image), len(contents), link, info, 4, entries))
        image += contents
    image += bytes((-len(image)) % 4)
    shoff = len(image)
    image += b''.join(headers)
    image[:52] = (b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack(
        '<HHIIIIIHHHHHH', elf_type, machine, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(headers), 1))
    return bytes(image)


class NativeControllerPolicyTests(unittest.TestCase):
    def test_selected_path(self):
        for path in ('driver.elf', 'drivers/usb-controller-esp32s3/driver.elf', 'a' * 123 + '.elf'):
            self.assertEqual(policy.relative_path(path), path)
        for path in ('', '/driver.elf', '../driver.elf', 'a/../driver.elf', './driver.elf',
                     'a//driver.elf', 'x\\driver.elf', 'x:driver.elf', 'a.json', '.x.elf',
                     'x..elf', 'x' * 124 + '.elf', 'x".elf', 'x\n.elf', 'é.elf',
                     'a' * 190 + '/driver.elf', None):
            with self.subTest(path=path), self.assertRaises(ValueError):
                policy.relative_path(path)

    def test_canonical_inventory_is_pinned(self):
        self.assertEqual(policy.IMPORTS, tuple(sorted(set(policy.IMPORTS))))
        self.assertEqual(len(policy.IMPORTS), 44)
        self.assertEqual(hashlib.sha256(('\n'.join(policy.IMPORTS) + '\n').encode()).hexdigest(),
                         policy.IMPORT_FILE_SHA256)

    def test_abi_relevant_profile_is_pinned_separately_from_product(self):
        canonical = json.dumps(policy.ABI_PROFILE, sort_keys=True, separators=(',', ':')).encode()
        self.assertEqual(hashlib.sha256(canonical).hexdigest(), policy.ABI_PROFILE_SHA256)
        for key in policy.ABI_PROFILE:
            evidence = qualification()
            evidence['abi_profile'][key] += 1
            evidence['abi_profile_sha256'] = hashlib.sha256(json.dumps(
                evidence['abi_profile'], sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            with self.subTest(key=key), self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)
        for mutation in (lambda p: p.update(tick_hz=1000.0),
                         lambda p: p.update(use_mutexes=True),
                         lambda p: p.pop('tick_hz'),
                         lambda p: p.update(product_partition='different')):
            evidence = qualification()
            mutation(evidence['abi_profile'])
            with self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)
        # A different whole product config is allowed with the same reviewed ABI
        # profile, provided the evidence's provenance records agree internally.
        evidence = qualification()
        evidence['sdkconfig_sha256'] = '3' * 64
        evidence['source_hashes']['/checkout/build/config/sdkconfig.h'] = '3' * 64
        evidence['compile_commands_sha256'] = '4' * 64
        evidence['source_hashes']['/checkout/build/compile_commands.json'] = '4' * 64
        policy.inspect_qualification(evidence)

    def test_unpinned_bytes_never_generate(self):
        for content in (b'', b'{}', b'\x7fELF', elf_fixture(), bytes(policy.PINNED_SIZE)):
            with self.subTest(size=len(content)), self.assertRaises(ValueError):
                policy.render(content, 'driver.elf', qualification())

    def test_public_descriptor_structure(self):
        self.assertEqual(policy._inspect_elf(elf_fixture()), list(policy.IMPORTS))
        for kwargs in ({'machine': 3}, {'elf_type': 2}, {'imports': policy.IMPORTS[:-1]},
                       {'imports': (*policy.IMPORTS[:-1], 'xOtherImport')},
                       {'export_name': 'forged_get'}, {'export_info': 0x22},
                       {'descriptor_size': 36}, {'omitted_section': '.symtab'},
                       {'omitted_section': '.dynsym'}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                policy._inspect_elf(elf_fixture(**kwargs))
        values = [2, 40, 0x2000, 0x2000 + len(policy.DRIVER_ID) + 1, 1,
                  0x3028, 0x1004, 0x1008, 0x100c, 0x1010]
        for index, value in ((0, 1), (1, 36), (2, 0x2001), (3, 0x2000),
                             (4, 2), (5, 0), (6, 0), (7, 0x3000), (8, 0), (9, 0)):
            changed = list(values)
            changed[index] = value
            with self.subTest(field=index), self.assertRaises(ValueError):
                policy._inspect_elf(elf_fixture(descriptor=changed))

    def test_evidence_accepts_exact_identity(self):
        policy.inspect_qualification(qualification())
        self.assertEqual(policy.PROVIDER_ABI, 2)
        self.assertEqual(policy.PROVIDER_SIZE, 40)
        self.assertEqual(policy.OS_CPU_ABI, 2)
        self.assertEqual(policy.DIAGNOSTIC_ABI, 1)

    def test_evidence_requires_all_fields(self):
        for key in qualification():
            evidence = qualification()
            del evidence[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)
        for value in ([], None, 'claim'):
            with self.assertRaises(ValueError):
                policy.inspect_qualification(value)

    def test_evidence_cannot_change_trusted_identity(self):
        updates = {
            'schema_version': True, 'status': 'qualified', 'driver_id': 'other',
            'version': '0.1.24', 'file_name': 'changed.elf', 'architecture': 'xtensa',
            'target': 'esp32', 'provider_abi': 1, 'os_cpu_abi': 1,
            'native_cohort': 'legacy', 'idf_commit': 'f' * 40, 'usb_commit': '0' * 40,
            'usb_version': '1.5.0', 'compiler': '14.2.0', 'compiler_distribution': 'other',
            'optimization': '-Og', 'abi_profile': {}, 'abi_profile_sha256': 'f' * 64,
            'requires': [{'capability': 'platform.usb.phy.resource', 'api': 1}],
            'provides': [{'capability': policy.CAPABILITY, 'api': True}],
            'size': float(policy.PINNED_SIZE), 'sha256': 'a' * 64,
            'imports': list(reversed(policy.IMPORTS)), 'usb_config': {},
        }
        for key, value in updates.items():
            evidence = qualification()
            evidence[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)
        for imports in (list(policy.IMPORTS[:-1]), [*policy.IMPORTS, 'printf'],
                        [*policy.IMPORTS[:-1], 'forged_private_import']):
            evidence = qualification()
            evidence['imports'] = imports
            with self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)

    def test_source_hashes_and_loader_map_fail_closed(self):
        mutations = [
            lambda e: e.update(sdkconfig_sha256='f' * 64),
            lambda e: e.update(compile_commands_sha256='f' * 64),
            lambda e: e.update(sdkconfig_sha256='not-a-digest'),
            lambda e: e.update(source_hashes={}),
            lambda e: e['source_hashes'].update({'/another/config/sdkconfig.h': '1' * 64}),
            lambda e: e['source_hashes'].update({'/bad': 'x'}),
            lambda e: e['source_hashes'].update({
                '/checkout/Drivers/usb_controller_esp32s3/idf61-imports.txt': 'f' * 64}),
            lambda e: e['loader_map'].update(relocations_examined=0),
            lambda e: e['loader_map'].update(relocations_examined=1078.0),
            lambda e: e['loader_map'].update(unmapped_relocations=[{}]),
            lambda e: e['loader_map'].update(unmapped_relative_values=[{}]),
            lambda e: e['loader_map'].update(unmapped_executable_sections=['.new.text']),
            lambda e: e['loader_map'].pop('unmapped_relocations'),
            lambda e: e['loader_map']['absolute_peripheral_relocations'][0].update(address='0x60000000'),
        ]
        for mutation in mutations:
            evidence = qualification()
            mutation(evidence)
            with self.assertRaises(ValueError):
                policy.inspect_qualification(evidence)

    def test_duplicate_and_oversized_json_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'qualification.json'
            for content in ('{"os_cpu_abi":1,"os_cpu_abi":2}',
                            '{"nested":{"value":0,"value":1}}', ' ' * (1024 * 1024 + 1)):
                path.write_text(content)
                with self.assertRaises(ValueError):
                    policy.read_qualification(path)
            path.write_text(json.dumps(qualification()))
            self.assertEqual(policy.read_qualification(path), qualification())

    def test_render_has_no_implicit_selection_or_extra_dependency(self):
        with patch.object(policy, 'inspect_controller', return_value=list(policy.IMPORTS)):
            generated = policy.render(b'fixture', 'drivers/driver.elf', qualification())
            self.assertEqual(generated, policy.render(b'fixture', 'drivers/driver.elf', qualification()))
            for field in (policy.PINNED_SHA256, policy.NATIVE_COHORT,
                          '"usb.controller",1,2,' + str(policy.PINNED_SIZE),
                          'imports,44,requirements,1,1', '{"board.power.vbus",1}',
                          'Public provider descriptor ABI 2/40 bytes'):
                self.assertIn(field, generated)
            self.assertNotIn('platform.usb.phy.resource', generated)
            self.assertNotIn('#define RISC_NATIVE_PROVIDER_POLICY_HEADER', generated)
            with self.assertRaises(ValueError):
                policy.render(b'fixture', 'driver.elf', {})

    def test_invalid_cli_does_not_write_or_replace_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            image, evidence, output = (directory / n for n in ('driver.elf', 'qualification.json', 'policy.h'))
            image.write_bytes(elf_fixture())
            evidence.write_text(json.dumps(qualification()))
            command = [sys.executable, policy.__file__, '--controller', str(image),
                       '--qualification', str(evidence), '--relative-elf-path', 'driver.elf',
                       '--output', str(output)]
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            output.write_text('preserved')
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_text(), 'preserved')
            for input_path in (image, evidence):
                original = input_path.read_bytes()
                alias_command = command[:-1] + [str(input_path)]
                result = subprocess.run(alias_command, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'must not replace', result.stderr)
                self.assertEqual(input_path.read_bytes(), original)

    @unittest.skipUnless(shutil.which(os.environ.get('CXX', 'g++')), 'C++ compiler not available')
    def test_generated_header_compiles_and_default_selection_stays_empty(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            header = directory / 'policy.h'
            with patch.object(policy, 'inspect_controller', return_value=list(policy.IMPORTS)):
                header.write_text(policy.render(b'fixture', 'driver.elf', qualification()))
            source = directory / 'check.cpp'
            source.write_text('''#include <cassert>
#include "runtime/drivers/SelectedNativeProviderPolicyV1.h"
int main() {
  using namespace RuntimeProviders;
  const auto selected = selectedNativeProviderPoliciesV1();
#if EXPECT_SELECTED
  assert(selected.count == 1 && selected.entries);
  const auto &p = selected.entries[0];
  assert(p.osCpuAbi == 2 && p.diagnosticAbi == 1 && p.importCount == 44);
  assert(p.requirementCount == 1 && p.requirements[0].api == 1);
  assert(!std::strcmp(p.requirements[0].capability, "board.power.vbus"));
  assert(nativeProviderPolicySetValid(selected) == bool(EXPECT_VALID));
#else
  assert(!selected.count && !selected.entries && nativeProviderPolicySetValid(selected));
#endif
}
''')
            for selected, native in ((False, True), (True, True), (True, False)):
                executable = directory / 'check'
                command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                           '-I' + str(Path(policy.__file__).resolve().parents[1] / 'src'),
                           '-I' + str(directory), '-DEXPECT_SELECTED=' + str(int(selected)),
                           '-DEXPECT_VALID=' + str(int(native)), str(source), '-o', str(executable)]
                if native:
                    command.append('-DRISC_NATIVE_ESP_IDF=1')
                if selected:
                    command.append('-DRISC_NATIVE_PROVIDER_POLICY_HEADER="policy.h"')
                subprocess.run(command, check=True, capture_output=True)
                subprocess.run([str(executable)], check=True, capture_output=True)

    @unittest.skipUnless(os.environ.get('IDF61_CONTROLLER_ELF') and os.environ.get('IDF61_QUALIFICATION'),
                         'exact native controller and qualification evidence not supplied')
    def test_exact_image_and_cli_check_mode(self):
        image = Path(os.environ['IDF61_CONTROLLER_ELF'])
        evidence_path = Path(os.environ['IDF61_QUALIFICATION'])
        data = image.read_bytes()
        evidence = policy.read_qualification(evidence_path)
        generated = policy.render(data, 'driver.elf', evidence)
        self.assertEqual(policy.inspect_controller(data), list(policy.IMPORTS))
        for offset in (0, len(data) // 2, len(data) - 1):
            changed = bytearray(data)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                policy.render(bytes(changed), 'driver.elf', evidence)
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / 'policy.h'
            command = [sys.executable, policy.__file__, '--controller', str(image),
                       '--qualification', str(evidence_path), '--relative-elf-path', 'driver.elf']
            subprocess.run(command + ['--output', str(header)], check=True, capture_output=True)
            self.assertEqual(header.read_text(), generated)
            subprocess.run(command + ['--check', str(header)], check=True, capture_output=True)
            header.write_text(generated + '\n// changed\n')
            self.assertNotEqual(subprocess.run(command + ['--check', str(header)], capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
