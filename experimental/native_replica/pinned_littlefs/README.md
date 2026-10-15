# Pinned private layout declarations

Unmodified upstream private/public declarations used by the explicit
`RISC_NATIVE_APP_DATA_MOUNT=1` binding. The native SDK6 component described below
supplies a separately qualified implementation without editing legacy SDK libraries.

- `littlefs_api.h`: joltwallet/esp_littlefs commit
  `41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317`, source SHA-256
  `cc806a02463db1516ed1a144189bc9267168b6cd79b543e95fb12099eeed18c2`
- `littlefs/lfs.h`: littlefs-project/littlefs commit
  `f53a0cc961a8acac85f868b431d2f3e58e447ba3`, source SHA-256
  `a8c8d70f0863fbbc46ce17c5dc7673b40f1b3c9e7e10f3bf33fd28f03dc67703`
- `littlefs/lfs_util.h`: same LittleFS commit, SHA-256
  `03e912a6e9894c9d10c61f5da22b89ebe0bb778af67972d7b67a5f160731bf72`

Upstream copyright and license headers remain intact. The consumer must match
these exact vendor sources/configuration; version strings alone are insufficient.
No ordinary target selects this binding or these declarations.

## Native SDK6 dependency port

`vendor/` additionally contains the minimum unmodified implementation at those
same commits, including the upstream config implementation and Kconfig. The
complete SHA-256 inventory is `vendor.sha256.json`, including the byte-exact
upstream license texts alongside their sources:

- `vendor/LICENSE`: [ESP LittleFS MIT license](https://github.com/joltwallet/esp_littlefs/blob/41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317/LICENSE),
  SHA-256 `b6bbf3bd17c2b5532bc25a7acf1760faac0b995d6e0ad8bec29435166727d4c5`.
- `vendor/littlefs/LICENSE.md`: [LittleFS BSD-3-Clause license](https://github.com/littlefs-project/littlefs/blob/f53a0cc961a8acac85f868b431d2f3e58e447ba3/LICENSE.md),
  SHA-256 `0cb4ff1daf5fdc1359c6a6ee3116092f08fc100c9d58b1b77ab17bfd801f856d`.

The existing `LICENSE-esp-littlefs.txt` and `LICENSE-littlefs.txt` are retained;
they carry the same notices with normalized whitespace. `prepare_idf6.py` checks
all source/license hashes and duplicate private
headers, then stages the bounded ABI/mtime32 wrapper patch without changing the
vendor originals or core. `mtime32.h` freezes the shipped 4-byte signed
little-endian seconds attribute independently of native libc time_t.

See `idf/components/risc_littlefs/README.md` for the exact configuration,
consumer link wraps, compatibility changes, and qualification commands.
