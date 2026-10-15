#!/usr/bin/env python3
"""Retired Arduino publisher; historical version checks remain testable offline.

Current native versions use publish_native_idf61_release.py with exact original
CI artifacts. This entry point has no publication or network authority.
"""
import argparse

from check_versions import version


def decision(record, releases):
    """Retain the historical immutable-version decision as a pure regression."""
    previous = [r for r in releases if r['tag_name'].startswith('firmware-v')]
    for release in previous:
        value = release['tag_name'].removeprefix('firmware-v')
        if version(value) > version(record['version']):
            raise ValueError('source version is older than an existing release')
    matches = [r for r in previous if r['tag_name'] == record['tag']]
    if len(matches) > 1:
        raise ValueError('duplicate release tag')
    return matches[0] if matches else None


def main(source):
    # Fail before artifact reads, credential access, subprocesses or network.
    raise ValueError(
        'Legacy Arduino publication is retired on this source. '
        'Use the guarded exact-artifact native-idf61-release.yml transport; '
        'historical Arduino builds are regression-only.'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-sha', required=True)
    args = parser.parse_args()
    main(args.source_sha)
