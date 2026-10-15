#!/usr/bin/env python3
"""Record clean CI source custody and verify exact released input bytes.

This is workflow bookkeeping, not a product qualification or build receipt.
Product qualification belongs to the product's same-process fresh producer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return {'bytes': path.stat().st_size, 'sha256': value.hexdigest()}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def source_record(root, revision):
    root = root.resolve()
    require(re.fullmatch('[0-9a-f]{40}', revision), 'Full immutable source revision required')
    require(root.stat().st_uid == os.getuid(), 'Source must be owned by this shell user: ' + str(root))
    require(git(root, 'rev-parse', '--show-toplevel') == str(root), 'Source must be a repository root')
    require(git(root, 'rev-parse', 'HEAD') == revision, 'Source HEAD differs: ' + str(root))
    require(not git(root, 'status', '--porcelain', '--untracked-files=all'), 'Source is dirty: ' + str(root))
    # Keep leading status characters: a space means an exact initialized pin.
    submodules = subprocess.check_output(
        ['git', '-C', str(root), 'submodule', 'status', '--recursive'], text=True).splitlines()
    require(all(line.startswith(' ') for line in submodules), 'Submodule is missing or changed: ' + str(root))
    return {'path': str(root), 'revision': revision,
            'tree': git(root, 'rev-parse', 'HEAD^{tree}'),
            'submodules': [line.strip() for line in submodules], 'clean': True}


def record_sources(rows):
    sources, roots = {}, {}
    for row in rows:
        role, location = row.split('=', 1)
        path, revision = location.rsplit('@', 1)
        require(re.fullmatch('[a-z_]+', role) and role not in roots, 'Invalid or duplicate source role')
        roots[role] = Path(path).resolve()
        sources[role] = source_record(roots[role], revision)
    # An alias (Watch as its own image helper) is fine; nested repos are not.
    for left in roots.values():
        for right in roots.values():
            require(left == right or not left.is_relative_to(right), 'Source roots must not be nested')
    return {'schema': 1, 'sources': sources, 'uid': os.getuid()}


def verify_baselines(product, contract_path, factory, factory_url, factory_sha256,
                     sd_archive=None, sd_url=None, sd_sha256=None):
    contract = json.loads(contract_path.read_text())
    require(contract['product'] == product, 'Released baseline belongs to another product')
    expected = {'bytes': contract['factory_bytes'], 'sha256': contract['factory_sha256']}
    require(factory_sha256 == expected['sha256'], 'Workflow factory digest differs from pinned contract')
    require(digest(factory) == expected, 'Released factory bytes differ from pinned contract')
    require(factory_url.startswith('https://github.com/'), 'Expected a verified HTTPS release asset URL')
    result = {'schema': 1, 'product': product, 'contract': digest(contract_path),
              'factory': dict(expected, url=factory_url)}
    if product == 'x4':
        require(sd_archive is not None and sd_url and sd_sha256, 'Complete X4 SD release archive required')
        expected_sd = {key: contract['sd'][key] for key in ('bytes', 'sha256')}
        require(sd_sha256 == expected_sd['sha256'], 'Workflow SD digest differs from pinned contract')
        require(digest(sd_archive) == expected_sd, 'Complete X4 SD archive bytes differ from pinned contract')
        require(sd_url.startswith('https://github.com/'), 'Expected a verified HTTPS SD release asset URL')
        result['sd_archive'] = dict(expected_sd, url=sd_url)
    else:
        require(sd_archive is None and sd_url is None and sd_sha256 is None,
                'Unexpected SD archive for this product')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    sources = commands.add_parser('sources')
    sources.add_argument('--source', action='append', required=True)
    sources.add_argument('--output', type=Path, required=True)
    baseline = commands.add_parser('baselines')
    for name in ('product', 'factory-url', 'factory-sha256'):
        baseline.add_argument('--' + name, required=True)
    for name in ('contract', 'factory', 'output'):
        baseline.add_argument('--' + name, type=Path, required=True)
    baseline.add_argument('--sd-archive', type=Path)
    baseline.add_argument('--sd-url')
    baseline.add_argument('--sd-sha256')
    args = parser.parse_args()
    if args.command == 'sources':
        result = record_sources(args.source)
    else:
        result = verify_baselines(args.product, args.contract, args.factory,
                                  args.factory_url, args.factory_sha256,
                                  args.sd_archive, args.sd_url, args.sd_sha256)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(args.command + ': exact CI input verification PASS')


if __name__ == '__main__':
    main()
