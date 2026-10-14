#!/usr/bin/env python3
"""Offline dependency/placement planning before existing provisioning admission.

This is not a downloader, manifest verifier, capability grant resolver or device
installer. All catalog URLs/digests and native capabilities are owner inputs.
"""
import re
from provision_profile import relative, source, spiffs_charge
from release_assets import require


def capability(value):
    require(isinstance(value, dict) and set(value) == {'capability', 'api'}, 'invalid capability declaration')
    name, api = value['capability'], value['api']
    require(isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9_.-]{1,95}', name)
            and type(api) is int and 0 < api <= 0xffffffff, 'invalid capability identity')
    return name, api


def plan(packages, core, bootstrap, *, native=(), bindings=None, sd_ready=False,
         internal_bytes=0x510000, sd_bytes=0, base_internal_sizes=()):
    """Resolve exact API dependencies, keeping SD bootstrap closure internal.

    `bindings` maps (consumer id, capability, api) to the selected provider id.
    Missing/ambiguous providers and cycles reject before any output is returned.
    A package has id, kind, requires, provides, files. Each file explicitly pins
    internal_path, sd_path, bytes, sha256, url. All packages are SD-preferred
    except bootstrap and its transitive dependency closure. A package that does
    not fit the remaining SD payload budget stays together on internal storage;
    the existing SPIFFS accounting must still admit the complete internal set.
    This selection occurs before mutation, never as an I/O-error retry.
    """
    require(isinstance(packages, list) and len(packages) <= 128, 'catalog bound')
    require(type(sd_ready) is bool and type(sd_bytes) is int and sd_bytes >= 0, 'media state')
    bindings = {} if bindings is None else bindings
    require(isinstance(bindings, dict), 'invalid bindings')
    native = {capability(item) for item in native}
    entries, providers = {}, {}
    for p in packages:
        require(isinstance(p, dict) and set(p) == {'id', 'kind', 'requires', 'provides', 'files'}, 'package fields')
        ident = p['id']
        require(isinstance(ident, str) and re.fullmatch(r'[A-Za-z0-9_.-]{1,95}', ident)
                and ident not in entries, 'duplicate/invalid package identity')
        require(p['kind'] in ('application', 'driver'), 'package kind')
        require(isinstance(p['requires'], list) and len(p['requires']) <= 24
                and isinstance(p['provides'], list) and len(p['provides']) <= 16, 'capability bound')
        needs = [capability(c) for c in p['requires']]
        offers = [capability(c) for c in p['provides']]
        require(len(set(needs)) == len(needs) and len(set(offers)) == len(offers), 'duplicate capability')
        require(not offers or p['kind'] == 'driver', 'app cannot provide driver capability')
        require(isinstance(p['files'], list) and 0 < len(p['files']) <= 16, 'package file bound')
        for f in p['files']:
            require(isinstance(f, dict) and set(f) == {'internal_path', 'sd_path', 'bytes', 'sha256', 'url'}, 'file fields')
            relative(f['internal_path']); relative(f['sd_path'], 192); source(f['url'])
            require(type(f['bytes']) is int and 0 < f['bytes'] <= 8 * 1024 * 1024, 'file byte bound')
            require(isinstance(f['sha256'], str) and re.fullmatch(r'[a-f0-9]{64}', f['sha256']), 'file digest')
        entries[ident] = p
        for offer in offers:
            providers.setdefault(offer, []).append(ident)
    require(isinstance(core, (list, tuple)) and isinstance(bootstrap, (list, tuple))
            and core and bootstrap and len(core) <= 128 and len(bootstrap) <= 128, 'root packages required')
    visiting, visited, edges, order = set(), set(), {}, []

    def visit(ident):
        require(isinstance(ident, str) and ident in entries, 'missing selected package')
        require(ident not in visiting, 'dependency cycle')
        if ident in visited:
            return
        visiting.add(ident)
        edges[ident] = []
        for req in entries[ident]['requires']:
            cap = capability(req)
            key = (ident, *cap)
            if cap in native:
                require(key not in bindings, 'binding conflicts with native capability')
                continue
            choices = providers.get(cap, [])
            chosen = bindings.get(key)
            if chosen is None:
                require(len(choices) == 1, 'missing or ambiguous dependency')
                chosen = choices[0]
            require(chosen in choices, 'binding does not provide capability')
            edges[ident].append(chosen)
            visit(chosen)
        visiting.remove(ident); visited.add(ident); order.append(ident)

    for ident in [*bootstrap, *core]:
        visit(ident)
    used_bindings = {(ident, *capability(req)) for ident in visited for req in entries[ident]['requires']}
    require(set(bindings) <= used_bindings, 'unused binding')
    internal = set()

    def pin(ident):
        if ident in internal:
            return
        internal.add(ident)
        for dependency in edges[ident]:
            pin(dependency)

    for ident in bootstrap:
        pin(ident)
    files, paths, sizes, sd_used = [], set(), list(base_internal_sizes), 0
    for ident in order:
        package_bytes = sum(f['bytes'] for f in entries[ident]['files'])
        medium = ('internal' if ident in internal or not sd_ready or
                  package_bytes > sd_bytes - sd_used else 'sd')
        for f in entries[ident]['files']:
            path = f['internal_path'] if medium == 'internal' else f['sd_path']
            require(not any(m == medium and (p == path or p.startswith(path + '/') or path.startswith(p + '/'))
                            for m, p in paths), 'duplicate/prefix-colliding destination')
            paths.add((medium, path))
            if medium == 'internal':
                sizes.append(f['bytes'])
            else:
                sd_used += f['bytes']
            files.append({'package': ident, 'medium': medium, 'path': path,
                          'bytes': f['bytes'], 'sha256': f['sha256'], 'url': f['url']})
    require(len(files) <= 128, 'selected file bound')
    # Reuse the production profile's pinned SPIFFS accounting, including receipt
    # and reserved blocks. SD capacity is payload-only; its writer must account
    # for staging, filesystem allocation units and checked readback separately.
    charged, available = spiffs_charge(sizes, internal_bytes)
    require(charged <= available, 'native store capacity exceeded')
    require(sd_used <= sd_bytes, 'SD payload capacity exceeded')
    return {'schema': 'riscrte.installation-plan', 'schema_version': 1,
            'activation_order': order, 'bootstrap_internal': sorted(internal),
            'files': files, 'internal_pages': charged, 'sd_payload_bytes': sd_used,
            'requires_native_admission': True}
