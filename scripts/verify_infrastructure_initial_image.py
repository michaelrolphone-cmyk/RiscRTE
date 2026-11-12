#!/usr/bin/env python3
"""Verify a complete first-install image against its pinned component manifest."""
import hashlib
import json
import re
from pathlib import Path
import struct
import sys

import paired_bank_images
import spiffs_image

root = Path(sys.argv[1])
release = json.loads((root / 'direct-image.json').read_text())
manifest = json.loads((root / 'flash-manifest.json').read_text())
image = (root / release['asset']).read_bytes()
def require(ok, message):
    if not ok:
        raise ValueError(message)
require(len(image) == release['bytes'] == manifest['flash_bytes'] == 0x1000000, 'flash size')
require(hashlib.sha256(image).hexdigest() == release['sha256'], 'whole-image hash')
require(image[0] == 0xe9 and struct.unpack_from('<H', image, 12)[0] == 9, 'ESP32-S3 header')
require(release['source'] == manifest['source'], 'source identities')
require(manifest['layout'] == 'riscrte-paired-appdata-v2' and manifest['layout_abi'] == 2, 'layout')
regions = []
for entry in manifest['files']:
    if 'offset' not in entry:
        continue
    start, end = entry['offset'], entry['offset'] + entry['size_bytes']
    require(0 <= start < end <= len(image), entry['path'] + ' bounds')
    require(not any(start < b and a < end for a, b in regions), 'overlap')
    require(hashlib.sha256(image[start:end]).hexdigest() == entry['sha256'], entry['path'])
    regions.append((start, end))
cursor = 0
for start, end in sorted(regions):
    require(all(b == 255 for b in image[cursor:start]), 'non-erased gap')
    cursor = end
require(all(b == 255 for b in image[cursor:]), 'non-erased tail')
pair = paired_bank_images.parse_record(image[0xff2000:0xff2060], app_data=True)
require(hashlib.sha256(image[0x10000:0x10000+pair[3]]).digest() == pair[6], 'paired firmware')
identities = set(re.findall(rb'RTE_SOURCE=([0-9a-f]{40})\x00', image[0x10000:0x10000+pair[3]]))
require(identities == {manifest['source']['runtime'].encode()}, 'embedded Runtime source identity')
store = image[0x2f0000:0x800000]
require(hashlib.sha256(store).digest() == pair[7], 'paired store')
contents = spiffs_image.read_image(store, 0x510000)
for entry in manifest['files']:
    if entry['path'].startswith('bootfs/'):
        name = entry['path'].removeprefix('bootfs/')
        data = contents[name]
        require(len(data) == entry['size_bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'], name)
require({'default.elf', 'infra-node.elf', 'infra-station.elf'} <= contents.keys(), 'complete provider cohort')
print('PASS: full ESP32-S3 image, all component hashes/ranges, erased gaps, paired records and embedded ELF cohort')
