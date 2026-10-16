#!/usr/bin/env python3
"""Pin current native SPIFFS inputs; verify the host geometry against native builds."""
import argparse
import hashlib
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent
SOURCE_BLOBS = {
    'spiffs.h': '58e49fbc24524a9465b463700498a15d519eb0dd',
    'spiffs_cache.c': 'e7cd4b7376c4c6c547f59c6a7034ec40f28fafdc',
    'spiffs_check.c': '8c7812ba156e9ecc96504dc35270a26d020cf6b4',
    'spiffs_gc.c': 'db1af4ccf6fa1239359d4df1e93fe92ea6db2152',
    'spiffs_hydrogen.c': '251a9a64cda7b6abcd30c05fd43eac2c15add4ba',
    'spiffs_nucleus.c': 'ab5cde12d3882660432c7183b0bef3db6c09c9c4',
    'spiffs_nucleus.h': '68191361a8d7ce39c89ba3ada9c6bb1e6479a0d6',
}
SDK_FILES = {
    'include/spiffs_config.h': '7a0164f5c360fa0faf5488bc36f68c23494a6a535b11ea0dd48b437088f050df',
    'Kconfig': '5155bcad962aa7feb0725f66cee6ee77ab3b6b2cb56165c83c057ccdc5fc846c',
    'spiffsgen.py': 'ed6ceee7fcecc02a0d427f274a695f15f160cc88f4744582d06b1d2ac0c8d6d9',
}


def defines(path):
    return sorted(line for line in path.read_text().splitlines()
                  if line.startswith('#define CONFIG_SPIFFS_'))


def prepare(sdk, output, spiffs=None, sdkconfig=None):
    component = sdk / 'components/spiffs'
    spiffs = spiffs or component / 'spiffs'
    for name, expected in SOURCE_BLOBS.items():
        data = (spiffs / 'src' / name).read_bytes()
        actual = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        if actual != expected:
            raise ValueError(f'Wrong native SPIFFS source: {name}')
    for name, expected in SDK_FILES.items():
        if hashlib.sha256((component / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f'Wrong native SPIFFS SDK input: {name}')
    # Captured from the supported generic target, then compared to every fresh
    # generic native build. Pin Kconfig as well so fixture-only runs cannot
    # silently follow changed defaults before that build runs.
    if sdkconfig is not None and defines(sdkconfig) != defines(ROOT / 'native_spiffs_sdkconfig.h'):
        raise ValueError('Native target SPIFFS configuration differs from host fixture')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(component / 'include/spiffs_config.h', output / 'spiffs_config.h')
    shutil.copyfile(ROOT / 'native_spiffs_sdkconfig.h', output / 'sdkconfig.h')
    print('Verified ESP-IDF 6.1 SPIFFS sources, producer and target geometry')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--spiffs', type=Path)
    parser.add_argument('--sdkconfig', type=Path)
    args = parser.parse_args()
    prepare(args.sdk, args.output, args.spiffs, args.sdkconfig)
