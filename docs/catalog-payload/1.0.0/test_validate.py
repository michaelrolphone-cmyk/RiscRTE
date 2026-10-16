#!/usr/bin/env python3
"""Positive and adversarial conformance tests for catalog payload version 1.0.0."""
import copy
import io
import json
import stat
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path

from jsonschema import Draft202012Validator
from make_examples import fixture, write_examples
from validate import (Invalid, ROOT, ZIP_TIME, canonical, inventory_hash, pack,
                      parse, schema_check, sha256, validate_container, validate_manifest)


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'container.zip'

    def run_case(self, mutate=None, context_mutate=None, contents_mutate=None,
                 kind='bootstrap', reject=None):
        m, c, contents = fixture(kind)
        if mutate:
            mutate(m)
        # Synthetic tests deliberately re-seal to reach semantic checks behind digest gates.
        m['payload']['inventory_sha256'] = inventory_hash(m['payload'])
        c['trusted_manifest_sha256'] = sha256(canonical(m))
        c['trusted_binding_sha256'] = [sha256(canonical(b)) for b in m['payload']['bindings']]
        c['trusted_install_sha256'] = sha256(canonical(m['firmware']['install']))
        c['trusted_consumer_sha256'] = [sha256(canonical(v['consumer'])) for v in m['payload']['volumes']]
        if contents_mutate:
            contents_mutate(contents)
        archive = pack(m, contents)
        c['trusted_container_sha256'] = sha256(archive)
        if context_mutate:
            context_mutate(c)
        self.path.write_bytes(archive)
        if reject:
            with self.assertRaisesRegex(Invalid, reject):
                validate_container(self.path, c, allow_fixture=True)
        else:
            result = validate_container(self.path, c, allow_fixture=True)
            self.assertEqual(result['status'], 'static-preflight-passed')
            self.assertFalse(result['mutation_performed'])
        return m, c

    def test_all_five_positive_containers(self):
        for kind in ('bootstrap', 'update-after', 'update-before', 'update-before-readonly', 'flat-root'):
            with self.subTest(kind=kind):
                self.run_case(kind=kind)

    def test_deterministic_examples(self):
        root = Path(self.temp.name)
        write_examples(root / 'a')
        write_examples(root / 'b')
        for source in (root / 'a').rglob('*'):
            if source.is_file():
                self.assertEqual(source.read_bytes(), (root / 'b' / source.relative_to(root / 'a')).read_bytes())
                self.assertEqual(source.read_bytes(), (ROOT / 'examples' / source.relative_to(root / 'a')).read_bytes())

    def test_schema_meta_validation(self):
        for filename in ('manifest.schema.json', 'context.schema.json', 'descriptor.schema.json'):
            Draft202012Validator.check_schema(json.loads((ROOT / filename).read_text()))
        for path in (ROOT / 'examples').glob('*/descriptor.json'):
            schema_check(json.loads(path.read_text()), 'descriptor.schema.json')

    def test_fixture_denied_by_default(self):
        m, c = self.run_case()
        with self.assertRaisesRegex(Invalid, 'fixture is not installable'):
            validate_container(self.path, c)

    def test_unknown_schema(self):
        self.run_case(lambda m: m.update(schema_version='2.0.0'), reject='manifest.schema')

    def test_unknown_property(self):
        self.run_case(lambda m: m['payload']['files'][0].update(delete=True), reject='manifest.schema')

    def test_unsupported_operation(self):
        self.run_case(lambda m: m['payload']['files'][0].update(operation='rename'), reject='manifest.schema')

    def test_backend_label_is_not_route(self):
        self.run_case(lambda m: m['payload']['bindings'][0].update(target='sd'), reject='manifest.schema')

    def test_unsafe_source_paths(self):
        for path in ('../escape', '/absolute', 'payload//x', 'payload/./x', 'payload/../x',
                     'payload/x\\y', 'payload/x\x00y', 'payload/x:', 'payload/x ',
                     'manifest.json', 'payload/%2e%2e'):
            with self.subTest(path=path):
                self.run_case(lambda m: m['payload']['files'][0].update(source=path), reject='path|source')

    def test_unsafe_destination_paths(self):
        for path in ('/', 'relative', '/a/../b', '/a//b', '/a/./b', '/a\\b', '/a\x7fb', '/a/'):
            with self.subTest(path=path):
                self.run_case(lambda m: m['payload']['files'][0].update(destination=path),
                              reject='path|directories|order|root|slash')

    def test_duplicate_destinations(self):
        self.run_case(lambda m: m['payload']['files'][1].update(destination=m['payload']['files'][0]['destination']),
                      reject='duplicate|non-unique')

    def test_duplicate_sources(self):
        self.run_case(lambda m: m['payload']['files'][1].update(source=m['payload']['files'][0]['source']),
                      reject='duplicate container')

    def test_missing_parent(self):
        self.run_case(lambda m: m['payload'].update(directories=[]), reject='parent directories')

    def test_extra_parent(self):
        self.run_case(lambda m: m['payload']['directories'].append(
            {'volume': 'content', 'path': '/unused', 'operation': 'ensure-directory'}), reject='parent directories')

    def test_parent_order(self):
        def mutate(m):
            m['payload']['files'][0]['destination'] = '/example/nested/data.bin'
            m['payload']['files'].sort(key=lambda f: f['destination'])
            m['payload']['directories'].insert(0, {'volume': 'content', 'path': '/example/nested',
                                                  'operation': 'ensure-directory'})
        self.run_case(mutate, reject='order: directories')

    def test_file_directory_collision(self):
        self.run_case(lambda m: m['payload']['files'][0].update(destination='/example'), reject='collision')

    def test_case_alias(self):
        def mutate(m):
            m['payload']['volumes'][0]['requirements']['case_semantics'] = 'ascii-insensitive'
            m['payload']['files'][0]['destination'] = '/example/EMPTY.txt'
        self.run_case(mutate, lambda c: c['namespaces'][0].update(case_semantics='ascii-insensitive'), reject='collision')

    def test_missing_file(self):
        self.run_case(contents_mutate=lambda c: c.pop('payload/data.bin'), reject='missing or unlisted')

    def test_extra_file(self):
        self.run_case(contents_mutate=lambda c: c.update({'payload/extra.bin': b'extra'}), reject='missing or unlisted')

    def test_length_mismatch(self):
        self.run_case(contents_mutate=lambda c: c.update({'payload/data.bin': b'x'}), reject='length mismatch')

    def test_hash_mismatch(self):
        self.run_case(contents_mutate=lambda c: c.update({'payload/data.bin': b'x' * 256}), reject='SHA-256 mismatch')

    def test_firmware_hash_mismatch(self):
        self.run_case(lambda m: m['firmware']['artifacts'][0].update(sha256='0' * 64), reject='SHA-256 mismatch')

    def test_totals_mismatch(self):
        self.run_case(lambda m: m['payload']['totals'].update(bytes=274), reject='totals mismatch')

    def test_inventory_hash_mismatch(self):
        m, c, _ = fixture()
        m['payload']['inventory_sha256'] = '0' * 64
        c['trusted_manifest_sha256'] = sha256(canonical(m))
        with self.assertRaisesRegex(Invalid, 'inventory digest'):
            validate_manifest(m, c, True)

    def test_product_profile_hardware_mismatch(self):
        for key in ('product_id', 'hardware_profile', 'hardware_revision'):
            with self.subTest(key=key):
                self.run_case(context_mutate=lambda c: c.update({key: 'other'}), reject='incompatible')

    def test_installed_firmware_mismatch(self):
        self.run_case(context_mutate=lambda c: c.update(installed_firmware_id='other'), reject='firmware mismatch')

    def test_missing_mapping(self):
        self.run_case(lambda m: m['payload']['bindings'][0].update(firmware_id='other'), reject='namespace mapping')

    def test_duplicate_mapping(self):
        self.run_case(lambda m: m['payload']['bindings'].append(copy.deepcopy(m['payload']['bindings'][0])),
                      reject='duplicate|non-unique')

    def test_unknown_namespace(self):
        self.run_case(lambda m: m['payload']['bindings'][0].update(namespace_id='8'), reject='not admitted')

    def test_namespace_numeric_limits(self):
        for namespace in ('0', '-1', '01', str(2**64)):
            with self.subTest(namespace=namespace):
                self.run_case(lambda m: m['payload']['bindings'][0].update(namespace_id=namespace), reject='schema|uint64')

    def test_readonly_flat_and_missing_operation(self):
        self.run_case(context_mutate=lambda c: c['namespaces'][0].update(writable=False), reject='read-only')
        self.run_case(context_mutate=lambda c: c['namespaces'][0].update(hierarchical=False), reject='hierarchical')
        self.run_case(context_mutate=lambda c: c['namespaces'][0].update(operations=['stat', 'get', 'create']),
                      reject='unsupported provider')

    def test_capacity_and_provider_bounds(self):
        for field, value, reason in [('write_budget_bytes', 272, 'budget'), ('max_file_bytes', 255, 'file too large'),
                                     ('max_path_bytes', 10, 'path too long'), ('max_component_bytes', 5, 'component too long')]:
            with self.subTest(field=field):
                self.run_case(context_mutate=lambda c: c['namespaces'][0].update({field: value}), reject=reason)

    def test_untrusted_pins(self):
        for key, value, reason in [('trusted_manifest_sha256', '0' * 64, 'catalog digest'),
                                   ('trusted_container_sha256', '0' * 64, 'container digest'),
                                   ('trusted_install_sha256', '0' * 64, 'install effects'),
                                   ('trusted_binding_sha256', ['0' * 64], 'namespace mapping'),
                                   ('trusted_consumer_sha256', ['0' * 64], 'consumer access')]:
            with self.subTest(key=key):
                self.run_case(context_mutate=lambda c: c.update({key: value}), reject=reason)

    def test_unsupported_flash(self):
        self.run_case(context_mutate=lambda c: c.update(supported_methods=['other']), reject='unsupported flash')

    def test_missing_flash_artifact(self):
        self.run_case(lambda m: m['firmware']['install'].update(artifact_ids=['other']), reject='artifact inventory')

    def test_preflash_survival(self):
        for effect in ('erased', 'unknown'):
            with self.subTest(effect=effect):
                self.run_case(lambda m: m['firmware']['install']['volume_effects'][0].update(effect=effect),
                              kind='update-before', reject='survival|erased')

    def test_consumer_firmware_mismatch(self):
        self.run_case(lambda m: m['payload']['volumes'][0]['consumer'].update(firmware_id='other'), reject='consumer firmware')

    def test_replace_guards_are_optional(self):
        self.run_case(lambda m: m['payload']['files'][0].update(previous=None), kind='update-after')
        self.run_case(lambda m: m['payload']['files'][0].pop('previous'), kind='update-after')
        self.run_case(lambda m: m['payload']['files'][0].update(previous={'sha256': '0' * 64}), kind='update-after')
        self.run_case(lambda m: m['payload']['files'][0]['previous'].update(owner='explicit-selected-other-owner'), kind='update-after')

    def test_create_cannot_replace(self):
        self.run_case(lambda m: m['payload']['files'][0].update(previous={'owner': 'fixture-app', 'bytes': 0,
                                                                       'sha256': '0' * 64}), reject='create cannot')

    def test_foreign_owner(self):
        self.run_case(lambda m: m['payload']['files'][0].update(owner='foreign'), reject='foreign release owner')

    def test_exact_unicode_and_wire_allowed_destinations(self):
        for path in ('/e\u0301', '/é', '/allowed<>:"|?*%', '/ends.', '/ends ', '/space here'):
            def mutate(m):
                m['payload']['files'][0]['destination'] = path
                m['payload']['files'].sort(key=lambda f: f['destination'].encode())
            with self.subTest(path=path):
                m, c = self.run_case(mutate)
                self.assertIn(path, [f['destination'] for f in m['payload']['files']])

    def test_provider_specific_naming_constraints(self):
        self.run_case(lambda m: m['payload']['files'][0].update(destination='/example/a%'),
                      lambda c: c['namespaces'][0].update(forbidden_characters='%'), reject='provider rejects')
        self.run_case(lambda m: m['payload']['files'][0].update(destination='/example/a.'),
                      lambda c: c['namespaces'][0].update(forbid_trailing_dot_space=True), reject='provider rejects')

    def test_factory_uses_evidence_not_label(self):
        self.run_case(lambda m: m['firmware']['install'].update(settings_effect='preserved'))

    def test_namespace_count_is_per_firmware(self):
        def invalid(c):
            original = copy.deepcopy(c['namespaces'][0])
            for i in range(20, 36):
                ns = copy.deepcopy(original)
                ns['namespace_id'] = str(i)
                c['namespaces'].append(ns)
        self.run_case(context_mutate=invalid, reject='too many admitted namespaces')
        def valid(c):
            for original in list(c['namespaces']):
                for i in range(20, 35):
                    ns = copy.deepcopy(original)
                    ns['namespace_id'] = str(i)
                    c['namespaces'].append(ns)
        self.run_case(context_mutate=valid, kind='update-before')

    def test_identifier_newline_rejected(self):
        for namespace in ('7\n', '7\r', '7 '):
            with self.subTest(namespace=namespace):
                self.run_case(lambda m: m['payload']['bindings'][0].update(namespace_id=namespace), reject='schema')
        self.run_case(lambda m: m['firmware'].update(id='fixture-v2\n'), reject='schema')
        self.run_case(lambda m: m['firmware']['artifacts'][0].update(sha256='0' * 64 + '\n'), reject='schema')

    def test_zip_prefix_suffix_and_gap_rejected(self):
        m, c, contents = fixture()
        data = pack(m, contents)
        for corrupted in (b'UNLISTED' + data, data + b'UNLISTED', data[:10] + b'UNLISTED' + data[10:]):
            c['trusted_container_sha256'] = sha256(corrupted)
            self.path.write_bytes(corrupted)
            with self.assertRaises((Invalid, zipfile.BadZipFile)):
                validate_container(self.path, c, True)

    def test_duplicate_json_member_and_nonfinite(self):
        for data in (b'{"a":1,"a":2}', b'{"n":NaN}', b'{"n":Infinity}'):
            with self.assertRaises(Invalid):
                parse(data)

    def test_zip_nonregular_compressed_and_duplicates(self):
        for attack in ('symlink', 'directory', 'compressed', 'duplicate', 'extra'):
            m, c, contents = fixture()
            contents['manifest.json'] = canonical(m)
            out = io.BytesIO()
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(out, 'w') as z:
                    for name, data in sorted(contents.items()):
                        entry = zipfile.ZipInfo(name, ZIP_TIME)
                        entry.create_system = 3
                        entry.external_attr = (stat.S_IFREG | 0o644) << 16
                        if name == 'payload/data.bin':
                            if attack == 'symlink':
                                entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                            if attack == 'directory':
                                entry.external_attr = (stat.S_IFDIR | 0o755) << 16
                            if attack == 'compressed':
                                entry.compress_type = zipfile.ZIP_DEFLATED
                            if attack == 'extra':
                                entry.extra = b'\x01\x00\x00\x00'
                        z.writestr(entry, data)
                        if attack == 'duplicate' and name == 'payload/data.bin':
                            z.writestr(entry, data)
            c['trusted_container_sha256'] = sha256(out.getvalue())
            self.path.write_bytes(out.getvalue())
            with self.subTest(attack=attack), self.assertRaises(Invalid):
                validate_container(self.path, c, True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
