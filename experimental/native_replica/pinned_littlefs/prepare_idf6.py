#!/usr/bin/env python3
"""Verify exact vendored sources and stage the documented native-IDF6 port.

No network, submodule update, source rewriting in place, or disk-format upgrade.
The generated ESP VFS wrapper alone receives the compatibility changes. Core,
private layout and all other vendor files remain byte-identical.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(destination):
    root = Path(__file__).resolve().parent
    vendor = root / 'vendor'
    manifest = json.loads((root / 'vendor.sha256.json').read_text())
    actual = {str(p.relative_to(vendor)) for p in vendor.rglob('*') if p.is_file()}
    if actual != set(manifest):
        raise SystemExit('Pinned LittleFS file inventory mismatch')
    for relative, expected in manifest.items():
        data = (vendor / relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit('Pinned LittleFS hash mismatch: ' + relative)
    for relative in ('littlefs_api.h', 'littlefs/lfs.h', 'littlefs/lfs_util.h'):
        if (root / relative).read_bytes() != (vendor / relative).read_bytes():
            raise SystemExit('Private LittleFS declaration differs from implementation: ' + relative)
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for relative in manifest:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(vendor / relative, target)
    source = (destination / 'esp_littlefs.c').read_text()
    def replace(old, new, count=1):
        nonlocal source
        if source.count(old) != count:
            raise SystemExit('LittleFS compatibility patch anchor mismatch: ' + old)
        source = source.replace(old, new)
    replace('#include "esp_littlefs.h"', '#include "esp_littlefs.h"\n#include "mtime32.h"\n#include <sys/types.h>\n#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)\ntypedef int risc_littlefs_truncate_result_t;\n#else\ntypedef ssize_t risc_littlefs_truncate_result_t;\n#endif')
    # IDF's truncate callback returns int. Old host fixtures use ssize_t;
    # enable the corrected signature only against the actual IDF6 headers.
    replace('static ssize_t vfs_littlefs_truncate(',
            'static risc_littlefs_truncate_result_t vfs_littlefs_truncate(' , count=2)
    replace('''    res = lfs_setattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            &t, sizeof(t));''', '''    uint8_t encoded[4];
    if (risc_littlefs_mtime_encode(t, encoded) != 0) return -1;
    res = lfs_setattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            encoded, sizeof(encoded));''')
    replace('''    time_t t;
    int size;
    size = lfs_getattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            &t, sizeof(t));''', '''    uint8_t encoded[4] = {0};
    int size;
    size = lfs_getattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            encoded, sizeof(encoded));''')
    replace('''    return t;
}
#endif //CONFIG_LITTLEFS_USE_MTIME''', '''    if (size != sizeof(encoded)) {
        errno = EIO; /* Present but wrong-width metadata is not a timestamp. */
        return -1;
    }
    return risc_littlefs_mtime_decode(encoded);
}
#endif //CONFIG_LITTLEFS_USE_MTIME''')
    for path in ('file->path', 'path'):
        old = f'    st->st_mtime = vfs_littlefs_get_mtime(efs, {path});'
        replace(old, '    errno = 0;\n' + old + '''
    if (errno && errno != ENODATA) {
        sem_give(efs);
        return -1;
    }''')
    (destination / 'esp_littlefs.c').write_text(source)
    shutil.copyfile(root / 'mtime32.h', destination / 'mtime32.h')
    print('Verified ESP LittleFS 1.14.1 / LittleFS 2.9 pins; staged native ABI and mtime32 port')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    prepare(parser.parse_args().destination)
