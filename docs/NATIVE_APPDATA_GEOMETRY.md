# Product-selected native application data

Explicit products may select `RISC_NATIVE_APP_DATA_OFFSET`, `RISC_NATIVE_APP_DATA_BYTES`
and `RISC_NATIVE_FLASH_BYTES`. Both the LittleFS mount and raw partition fence
use the same compile-time geometry. The historical default is unchanged
(0x270000 / 0x80000 in 16 MiB). Alignment, size and flash bounds are checked
at compile time; the discovered partition must match exactly and be unencrypted.

`RISC_NATIVE_STANDALONE_APP_DATA=1` explicitly mounts this existing backend
after bootfs for a non-paired product. It requires `RISC_PAIRED_APP_DATA` and
`RISC_NATIVE_APP_DATA_MOUNT=1`; these legacy feature names select the existing
backend, without enabling paired banks. A failed standalone mount stops boot.
Mounting never formats or grows storage. Provision a compatible filesystem in
the product image; full-image replacement destroys existing application data.

Ownership, asynchronous replacement, namespace quotas, retained cleanup and
resource fences are unchanged. Native NVS preferences remain separate.
Host qualification: `python test/test_native_appdata_layout.py`;
`python test/run_native_settings_mount_custody_test.py --output OUT`, repeated
with `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`. The latter uses the pinned real
LittleFS mount/workers and qualifies default geometry, not physical flash.
Product geometry also requires a new native link and partition/image audit.
