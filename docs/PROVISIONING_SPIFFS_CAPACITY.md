# Streamed SPIFFS provisioning capacity

This bound continues to govern schema1/2 file streaming. Runtime0.1.70 offers
[compact-image provisioning](COMPACT_IMAGE_PROVISIONING.md) with separate actual
image occupancy checks; it does not relax this append/index-garbage bound.

This is a host-verified admission estimate for the pinned ESP32-S3 storage
configuration. It does not qualify hardware timing, physical power loss, Wi-Fi,
TLS, or the whole boot graph. Every actual write, close, readback hash, inventory,
and graph-admission check must still succeed before selection.

## Pinned geometry and implementation

ESP-IDF v4.4.7 pins upstream SPIFFS at
[`0dbb3f71c5f6fae3747a9d935372773762baf852`](https://github.com/pellepl/spiffs/tree/0dbb3f71c5f6fae3747a9d935372773762baf852).
The installed Arduino 2.0.17 ESP32-S3 configuration uses 4096-byte blocks,
256-byte pages, 16-bit object/page indexes, 32-byte names, and 4-byte metadata.
There is one lookup page per block, leaving 15 allocatable pages. Each data page
holds 251 bytes. The 49-byte first index header holds 103 indexes; each 8-byte
continuation header holds 124.

Relevant primary sources:

- [IDF submodule pin](https://github.com/espressif/esp-idf/blob/v4.4.7/.gitmodules)
- [IDF filesystem configuration and mtime handling](https://github.com/espressif/esp-idf/blob/v4.4.7/components/spiffs/esp_spiffs.c)
- [SPIFFS page/index layouts](https://github.com/pellepl/spiffs/blob/0dbb3f71c5f6fae3747a9d935372773762baf852/src/spiffs_nucleus.h)
- [Sequential append implementation](https://github.com/pellepl/spiffs/blob/0dbb3f71c5f6fae3747a9d935372773762baf852/src/spiffs_nucleus.c)
- [GC spare blocks and bounded runs](https://github.com/pellepl/spiffs/blob/0dbb3f71c5f6fae3747a9d935372773762baf852/src/spiffs_gc.c)

## Admission calculation

For each nonempty payload file, and separately for the 32-byte
`.provision-sha256` object:

```
D = ceil(bytes / 251)
I = 1 + ceil(max(0, D - 103) / 124)
W = ceil(bytes / 8192)
charged_pages = D + 2*I + W - 1
```

`D + I` are live data/index pages. The additional `W + I - 1` conservatively
charges the mtime header replacement, subsequent write-flush size/header
replacements, and an extra header replacement at each continuation-index
boundary. It does not assume that this new write garbage has already been
reclaimed. Reserve four whole blocks and require:

```
sum(charged_pages) <= (partition_bytes / 4096 - 4) * 15
```

The partition must have a valid block-aligned size. The normal profile count,
file-size, path, target-configuration, and partition checks still apply. The
shared native calculation is `src/runtime/provisioning/SpiffsCapacity.h`.

`StoreFiles` accepts input chunks of at most 4096 bytes but coalesces them in an
8192-byte staging buffer. Output is explicitly unbuffered at the C stdio layer,
so libc does not split those flushes. Final partial data is flushed only at EOF;
abort discards it. Readback/hash checkpoints stay at most 4096 bytes. This uses
4 KiB more checked PSRAM than the previous staging buffer and no private GC API.

A simple 75% payload cap rejected the complete Watch store. Merely dropping that
cap is insufficient: the real SPIFFS harness reproduced a full error near the
end of a repeated install with direct 1024-byte writes. Coalescing fixes the
excessive index churn; the bound charges the remaining churn explicitly.

## Reproducible host proof

`test/run_provisioning_spiffs_test.sh` requires Linux/glibc, a host C/C++ compiler,
Git, OpenSSL headers/library, the pinned upstream SPIFFS source, and the installed
ESP32-S3 SDK directory. It verifies every upstream source Git-blob identity and
both target configuration hashes. It does not download or vendor source, access
a device, or format a filesystem.

```
bash test/run_provisioning_spiffs_test.sh \
  "$SPIFFS_SOURCE_ROOT" "$ESP32S3_SDK_ROOT" \
  "$GENERIC_SEED_IMAGE" "$STORE_DIRECTORY" "$NEW_OUTPUT_IMAGE"
```

Public deterministic CI fixtures contain arbitrary patterned bytes and no
product payloads. Generate three stores with:

```
python3 test/provisioning_spiffs_fixture.py "$NEW_FIXTURE_ROOT"
```

The subdirectories are `high-fill-83`, `boundary-3`, and `boundary-128`.
Boundary sizes are selected through the production Python page calculation;
the native harness verifies `fits()` and rejects one more byte. The fixtures
are not valid boot graphs and must never be flashed as product stores.

The test mounts a previously built generic seed image into a host NOR-flash
model and runs production `StoreFiles` against upstream SPIFFS through a small
stdio/directory adapter. It removes the old seed files and writes the complete
inventory. Its graph-admission callback is a stub; this test verifies filesystem
behavior, not graph correctness. A separate active seed image is checked
byte-for-byte and by SHA-256 after each scenario.

Coverage:

- 88 boundary/random file-size and 4096/8192-byte flush combinations, checking
  actual allocated plus obsolete pages against the formula and native helper
- Complete writes with 4096, 512, 37, and 1-byte input chunks
- Repeated replacement, remount, and SPIFFS consistency checks
- Last-file streamed hash mismatch and clean retry
- Almost-complete interruption and remounted retry
- Interruption at exactly an 8192-byte flush, refusal to resume the old stream,
  and clean retry
- Persistent readback corruption and clean retry
- Injected short `fwrite` failure and clean retry
- Admission refusal without publishing the digest, followed by clean retry
- Production `fits()` checks, invalid partition alignment, invalid/oversized
  files, and one-byte-over-budget rejection for exact-bound inventories

The verified 83-file Watch inventory contains 4,470,591 payload bytes. Including
the digest, it charges 18,810 pages against the ABI2 budget of 19,380 and ends with
18,075 live pages. Independent extraction with the existing mkspiffs tool
verified all 83 payload files byte-for-byte and the 32-byte digest.

Additional synthetic inventories passed the same complete scenarios:

- 3 files, 4,644,757 payload bytes: exactly 19,380 charged pages
- 128 files, 4,554,879 payload bytes: exactly 19,380 charged pages
- 128 equal-sized files, 4,594,304 payload bytes: 19,331 charged pages

`SANITIZE=1` enables ASan and UBSan. Upstream SPIFFS intentionally stores 16-bit
indexes at odd offsets with the pinned unaligned-index format; only alignment
UBSan is disabled in that unchanged upstream C. Application C++ keeps full
UBSan. This sandbox prevents LeakSanitizer process inspection, so local ASan/
UBSan verification uses `ASAN_OPTIONS=detect_leaks=0`; leak checking is not
claimed.
