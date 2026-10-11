# Temporary private Wi-Fi operations (working Runtime 0.2.20)

Exact base: `c3d750d113dbc79af87daaad316ab1d63d57dd60`, including the
hardware-confirmed boot fix and the retained-yield correction. No capacities,
export map, software selectors or existing request layouts change.

`RISC_RADIO_REQUEST_AP` adds a copied asynchronous WPA2 AP operation using IDF
4.4's default 192.168.4.1/24 DHCP interface, limited to one station. The device
must declare AP hardware feature bit 2. `RISC_RADIO_TEMPORARY` permits a new JOIN
or AP to suspend a healthy copied asynchronous station session. Nested overrides,
scans, pending setup/cleanup, service leases and live TCP/HTTP owners refuse.

The native worker retains only copied RAM credentials needed to restore the
original operation. Cancellation drains the temporary interface and restores the
old station before reporting completion and returning its original operation ID.
If restoration cannot be proven, custody remains retained; no automatic cleanup
retry or falsely successful exit is allowed. An originally off radio returns off.
Legacy borrowed/native station ownership that cannot be snapshotted refuses
without disconnecting it. All SDK configurations remain RAM-only with NVS disabled.
SDK logs are suppressed before credentials enter SDK configuration and restored
only after cleanup. The larger single native mailbox remains below 4 KiB.

Host verification uses the real generic Wi-Fi provider, CPU port, native async
worker and controlled SDK boundary. AP, queued cancellation, busy refusal,
successful station restoration, repeated terminal polls and failed restoration
are exercised. The existing radio lifecycle and asynchronous race suites pass
under UBSan on macOS. These are software checks, not RF/hardware qualification.

BLE confirmation, finite session expiry and WebDAV remain generic external
provider/application responsibilities. Full composition and device testing are
required before claiming router-free feature completion.
