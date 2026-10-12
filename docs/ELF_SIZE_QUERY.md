# Avoid walking a flash file merely to find its size

The ordinary ELF reader formerly sought to EOF and then back to zero immediately
after opening each file. On the pinned SPIFFS implementation, a large forward
seek finds the distant object-index page through the flash lookup table. This
does not load ELF content or improve admission.

The reader now uses `fstat()` for regular-file size. A successful read-only open
already starts at offset zero. Filesystems without regular-file `fstat()` retain
the exact old seek/rewind path. The existing 52-byte minimum, 8 MiB maximum,
allocation handling, bounded 4 KiB reads, cooperation/deadline checks, close
handling, full structural validation and managed-image admission remain intact.
No image cache, checksum, metadata scan or validation bypass is added.

## Source-bound reproduction

The SPIFFS submodule selected by ESP-IDF v4.4.7 is
`pellepl/spiffs@0dbb3f71c5f6fae3747a9d935372773762baf852`.
Its `SPIFFS_lseek()` locates a far object-index span;
`SPIFFS_fstat()` reads the already-open object header with `spiffs_stat_pix()`.
The latter header is already cached immediately after open.

A host HAL-memory replay of the exact Watch 1.1.12 bootfs, using the pinned
256-byte-page/4096-byte-block/name32/meta4 configuration and four open slots,
found 1,324 extra flash reads / 338,944 bytes for the default ELF's original
EOF/rewind sequence. Replacing only that size query with `SPIFFS_fstat()` reduced
these to zero additional HAL reads. Named-open and payload-read totals remained
identical for all 58 metadata files and eleven pre-frame module instances.
This is an operation count, not a claim of physical milliseconds saved.

That replay also identified substantially larger, separate named-open scans:
45,820 HAL calls / 10,231,240 bytes across the sequence, versus 4,642 HAL calls /
1,188,352 bytes for actual file payloads. Raising the SDK's `max_files` from four
to 32 barely changes the name scans. This change does not alter that mechanism.

## Tests

`run_performance_loader_test.sh` executes the production reader with real fixture
files and injected filesystem/RTOS boundaries. It checks regular-file `fstat`
without seeks; unsupported and nonregular fallbacks; either fallback seek failing;
negative, short and oversized reported lengths; and the existing read/deadline,
corruption, relocation, allocation, scope and code-publication regressions.
`run_image_byte_cache_test.sh` checks the updated operation counts while retaining
independent mappings, cache lifetime, failures, eviction and no-PSRAM fallback.
Both pass normally and with ASan/UBSan. Native ELF validator and resident object
export suites also remain unchanged and pass on suitable fixture stores.

The dedicated crown wake target runs the production reader/cache suites after
its exact-source single-job firmware build. Hardware wake timing is still pending.
