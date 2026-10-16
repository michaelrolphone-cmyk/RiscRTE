#!/usr/bin/env python3
"""Extract unchanged pinned IDF VFS callbacks; no independent flag model."""
import pathlib, re, sys
source = pathlib.Path(sys.argv[1]).read_text()
names = ['spiffs_res_to_errno', 'spiffs_mode_conv', 'vfs_spiffs_update_mtime',
         'vfs_spiffs_get_mtime', 'vfs_spiffs_open', 'vfs_spiffs_write',
         'vfs_spiffs_read', 'vfs_spiffs_close', 'vfs_spiffs_lseek',
         'vfs_spiffs_fstat', 'vfs_spiffs_fsync', 'vfs_spiffs_stat',
         'vfs_spiffs_unlink', 'vfs_spiffs_opendir', 'vfs_spiffs_closedir',
         'vfs_spiffs_readdir_r', 'vfs_spiffs_readdir', 'vfs_spiffs_telldir',
         'vfs_spiffs_seekdir', 'vfs_spiffs_ftruncate']
functions = []
for name in names:
    match = re.search(r'^static [^;\n]+\b' + name + r'\([^;]+?\)\s*\{', source, re.M)
    if not match:
        raise SystemExit('Missing SDK function: ' + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[match.start():end])
pathlib.Path(sys.argv[2]).write_text('\n\n'.join(functions) + '\n')
