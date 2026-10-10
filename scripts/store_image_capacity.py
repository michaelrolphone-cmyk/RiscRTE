"""Physical occupancy admission for pinned compact SPIFFS images.

Mirrors src/runtime/provisioning/StoreImageCapacity.h, including local header
safety before native SPIFFS string access. This is neither a file decoder nor
complete filesystem/reference/graph/hash validation. Those admissions are separate.

ESP-IDF 4.4.7 pins pellepl/spiffs at
0dbb3f71c5f6fae3747a9d935372773762baf852. Geometry/lookup/magic sources:
src/spiffs_nucleus.h:141-142,165-166,192-199,232-237;
src/spiffs_nucleus.c:330-338; src/spiffs_gc.c:280-283.
"""

import struct

BLOCK_BYTES = 4096
PAGE_BYTES = 256
PAGES_PER_BLOCK = 15
RESERVE_BLOCKS = 4
MAX_FILE_BYTES = 8 * 1024 * 1024


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _live_page_safe(page, object_id, partition_bytes):
    ident, span, flags = struct.unpack_from('<HHB', page)
    index = bool(object_id & 0x8000)
    _require(object_id & 0x7fff and ident == object_id and not flags & 3
             and flags & 0x80 and index != bool(flags & 4),
             'SPIFFS image live lookup/header mismatch or invalid page flags')
    if index and span == 0:
        # Pinned spiffs_hydrogen.c:1084 uses strcpy from this 32-byte field.
        # A NUL in the following metadata cannot make that copy safe.
        size = struct.unpack_from('<I', page, 8)[0]
        _require(0 in page[13:45] and flags & 0x40 and page[12] == 1
                 and 0 < size <= min(MAX_FILE_BYTES, partition_bytes),
                 'SPIFFS image unsafe object header name, type or size')


def inspect_reader(partition_bytes, read):
    """Read (offset, size) -> bytes in aligned 4096-byte sectors; fail closed.

    The caller must serialize the image against concurrent changes. A reader
    error, short read, invalid geometry, malformed free space, or inadequate
    physical capacity raises ValueError; no partial counts are returned.
    """
    _require(type(partition_bytes) is int and partition_bytes > 4 * BLOCK_BYTES
             and partition_bytes % BLOCK_BYTES == 0
             and partition_bytes // PAGE_BYTES <= 0xffff,
             'unsupported SPIFFS image geometry')
    _require(callable(read), 'SPIFFS image reader required')
    blocks = partition_bytes // BLOCK_BYTES
    budget = (blocks - RESERVE_BLOCKS) * PAGES_PER_BLOCK
    result = dict(occupied_pages=0, deleted_pages=0, free_pages=0, free_blocks=0)
    for block in range(blocks):
        try:
            sector = read(block * BLOCK_BYTES, BLOCK_BYTES)
        except Exception as error:
            raise ValueError('SPIFFS image read failed') from error
        _require(isinstance(sector, (bytes, bytearray, memoryview)),
                 'SPIFFS image reader must return bytes')
        try:
            sector = memoryview(sector).cast('B')
        except (TypeError, ValueError) as error:
            raise ValueError('SPIFFS image reader must return contiguous bytes') from error
        _require(len(sector) == BLOCK_BYTES, 'SPIFFS image short read')
        magic = (0x20140529 ^ PAGE_BYTES ^ (blocks - block)) & 0xffff
        _require(struct.unpack_from('<H', sector, PAGE_BYTES - 4)[0] == magic,
                 'SPIFFS image magic or partition length mismatch')
        _require(all(value == 0xff for value in sector[PAGES_PER_BLOCK * 2:PAGE_BYTES - 4]),
                 'SPIFFS image lookup padding is not erased')
        # The final uint16 is an erase counter, not an allocatable lookup entry.
        free_seen = False
        whole_block_free = True
        for entry, object_id in enumerate(struct.unpack_from('<15H', sector)):
            if object_id == 0xffff:
                free_seen = True
                _require(all(value == 0xff for value in
                             sector[(entry + 1) * PAGE_BYTES:(entry + 2) * PAGE_BYTES]),
                         'SPIFFS image free page is programmed')
                result['free_pages'] += 1
            else:
                _require(not free_seen, 'SPIFFS image lookup has an occupied page after a free page')
                whole_block_free = False
                result['occupied_pages'] += 1
                if object_id == 0:
                    result['deleted_pages'] += 1
                else:
                    _live_page_safe(sector[(entry + 1) * PAGE_BYTES:(entry + 2) * PAGE_BYTES],
                                    object_id, partition_bytes)
                _require(result['occupied_pages'] <= budget, 'SPIFFS image occupied-page budget exceeded')
        if whole_block_free:
            result['free_blocks'] += 1
    _require(result['free_blocks'] >= RESERVE_BLOCKS, 'SPIFFS image needs four entirely free blocks')
    return result


def inspect(image):
    """Inspect a bytes-like image; return occupancy counts or raise ValueError."""
    try:
        view = memoryview(image).cast('B')
    except (TypeError, ValueError) as error:
        raise ValueError('SPIFFS image bytes required') from error
    return inspect_reader(len(view), lambda offset, size: view[offset:offset + size])


def inspect_image(image, expected_partition_bytes):
    """Require an exact partition-sized bytes-like image, then inspect it."""
    _require(type(expected_partition_bytes) is int
             and expected_partition_bytes > RESERVE_BLOCKS * BLOCK_BYTES
             and expected_partition_bytes % BLOCK_BYTES == 0
             and expected_partition_bytes // PAGE_BYTES <= 0xffff,
             'unsupported SPIFFS image geometry')
    try:
        view = memoryview(image).cast('B')
    except (TypeError, ValueError) as error:
        raise ValueError('SPIFFS image bytes required') from error
    _require(len(view) == expected_partition_bytes, 'SPIFFS image length mismatch')
    return inspect(view)
