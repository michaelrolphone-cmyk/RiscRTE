# Pinned private layout declarations

Unmodified upstream private/public declarations used only by the explicit
`RISC_NATIVE_APP_DATA_MOUNT=1` binding. No vendor implementation is replaced.

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
