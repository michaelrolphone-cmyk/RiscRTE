#!/usr/bin/env python3
"""Encode only explicitly supplied ordinary test images, preserving exact recovery."""
import hashlib, pathlib, struct, sys, zlib
import lz4.block
for name in sys.argv[1:]:
    path=pathlib.Path(name)
    raw=path.read_bytes()
    assert 52 <= len(raw) <= 8*1024*1024 and raw[:4] == b'\x7fELF'
    payload=lz4.block.compress(raw,store_size=False)
    assert lz4.block.decompress(payload,uncompressed_size=len(raw)) == raw
    stored=struct.pack('<8s6I',b'RTEIMG1\0',32,1,len(raw),len(payload),zlib.crc32(raw)&0xffffffff,0)+payload
    path.write_bytes(stored)
    print(path.name, 'canonical',len(raw),hashlib.sha256(raw).hexdigest(),'physical',len(stored),hashlib.sha256(stored).hexdigest())
