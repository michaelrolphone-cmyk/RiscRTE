# Decoder provenance: RiscRTE-T-Watch-S3/scripts/read_only_spiffs.py at
# db5f6c5ba1a71ed3fcc22ff29847397a050b7ee4, MIT license; retained for generic
# offline image admission. Original repository:
# https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3
"""Read-only analysis of the pinned SPIFFS 256/4096, name32/meta4 images.

This does not install or execute mkspiffs and cannot create a flash image.
Format: upstream pellepl/spiffs src/spiffs_nucleus.h, inspected 2026-10-04.
Validate each live lookup/header and each file's referenced index/data relation.
This is a bounded decoder, not a general consistency checker or filesystem writer.
"""
from pathlib import PurePosixPath
import struct

if not __debug__:
    raise RuntimeError("Read-only SPIFFS verification requires Python assertions")


def read_image(data, expected_size=0x4f0000):
    assert expected_size in (0x4f0000, 0x510000)
    assert len(data) == expected_size
    page_count = len(data) // 256
    live = {}
    for block in range(len(data) // 4096):
        for entry in range(15):
            obj = struct.unpack_from('<H', data, block * 4096 + entry * 2)[0]
            if obj in (0, 65535):
                continue
            ix = block * 16 + 1 + entry
            raw = data[ix * 256:(ix + 1) * 256]
            ident, span, flags = struct.unpack_from('<HHB', raw)
            assert ident == obj and not flags & 3 and flags & 128
            assert bool(obj & 0x8000) == (not bool(flags & 4))
            live[ix] = (obj, span, flags, raw)
    indexes = {}
    for ix, (obj, span, flags, raw) in live.items():
        if obj & 0x8000:
            assert (obj, span) not in indexes
            indexes[obj, span] = ix
    result = {}
    for (obj, span), ix in indexes.items():
        if span:
            continue
        raw = live[ix][3]
        assert live[ix][2] & 64
        length = struct.unpack_from('<I', raw, 8)[0]
        assert raw[12] == 1 and length < len(data)
        name = raw[13:45].split(b'\0')[0].decode()
        assert name.startswith('/')
        name = name[1:]
        assert name and not PurePosixPath(name).is_absolute() and '\\' not in name
        assert str(PurePosixPath(name)) == name and '..' not in PurePosixPath(name).parts
        assert name not in result
        chunks = []
        for data_span in range((length + 250) // 251):
            if data_span < 103:
                index_span, index_entry, offset = 0, data_span, 49
            else:
                index_span, index_entry, offset = 1 + (data_span - 103) // 124, (data_span - 103) % 124, 8
            source = live[indexes[obj, index_span]][3]
            page = struct.unpack_from('<H', source, offset + index_entry * 2)[0]
            assert 0 < page < page_count and page in live
            ident, actual_span, flags, content = live[page]
            assert ident == (obj & 0x7fff) and actual_span == data_span and flags & 4
            chunks.append(content[5:])
        result[name] = b''.join(chunks)[:length]
    return result
