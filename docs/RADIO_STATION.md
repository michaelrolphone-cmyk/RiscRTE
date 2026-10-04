# Generic station radio, firmware 0.1.9

`platform.radio@1` is admitted only for a selected `radio.integrated@1` device
with compatible `espressif,esp32s3-wifi`, unit 0 and station feature bit 0. The
entire board and selected driver graph must validate before a module loads or
hardware is touched. Unselected declarations get no table, a missing native
backend fails closed, and another radio cannot claim the same physical unit.
The board may declare AP support; this bounded native slice does not implement
AP creation. `start_ap` returns false; `stop_ap` truthfully succeeds for an owned
station-only token because no AP exists.

## ABI and authority

The historical Garden radio prefix, including field order/types through
`addresses`, is unchanged. `GARDEN_RADIO_PREFIX_V1_SIZE` identifies that prefix.
`RiscRadioScanV1.h` defines a 16-entry copied scan snapshot and three appended
callbacks gated by `GARDEN_RADIO_SCAN_V1_SIZE`. No app or vendor pointer is
returned. SSIDs are at most 32 bytes plus NUL, passwords are empty or 8..63
bytes. The adapter does not log either input. Station state 0 is down, 1 joining, and 2 up;
up requires both AP association and a nonzero IPv4 address. Address arrays
contain IPv4 address, gateway, then netmask, each as four network-order octets.

One runtime owner task serializes calls. A monotonically allocated token grants
exclusive ownership. Claim itself does not initialize hardware or consume
credentials. Join and scan initialize lazily; joining during a scan, scanning
during a join, repeated active starts, invalid/stale tokens and wrong-owner
calls fail. The caller must cancel/leave before a retry. A thirty-second join deadline,
checked by status polling, stops the native attempt before reporting DOWN;
failed timeout cleanup retains ownership. There is no automatic
reconnection or SDK saved configuration; the app decides all retry/timeout/UI
policy and any separately authorized persistence.

Scan starts asynchronously and polls a bounded copied snapshot. Hidden SSIDs may
be empty, and unjoinable authentication types are explicitly UNSUPPORTED. The
native scan has a ten-second observation deadline. DONE and FAILED still own the
native session; the consumer must cancel/leave before joining or sleeping.
Cancel is idempotent when no scan exists and does not disconnect an unrelated
station attempt. Failed scan cleanup remains active and can be retried.

## Cleanup, sleep and app lifetime

`leave` cancels scans, clears SDK scan storage, disconnects, zeroes RAM station
configuration, stops/deinitializes Wi-Fi, removes owned handlers, deletes the
owned event loop and destroys the station netif. The backend does not adopt an
already-owned default event loop. Deleting the queue before any new attempt
prevents stale queued disconnect/scan events from changing a later retry.
Generation guards also reject callbacks from old registrations. No app code or
credential pointer enters a callback. IDF's one-time inactive TCP/IP service
cannot be deinitialized; it has no radio/netif/app callback ownership after leave.

A successful leave keeps the logical token but proves native idle, so an idle
boot-session provider may survive an app handoff or Light/Deep sleep. `release`
additionally drops that token. Port quiescence requires all claims released.
Active radio returns BUSY for Light/Deep; failed cleanup returns RETAINED.
Either state blocks app finalization, image unmapping and queued handoffs through
the existing Runtime retention barrier. Healthy RF activity alone does not
revoke a boot-session provider's bound storage. A separate compiled-in
`providerStorageSafe` callback preserves the previous poison, held-output, sleep,
transfer and I2S checks and additionally rejects radio cleanup failure. Runtime
uses it for provider admission and bound-KV reads/writes; ports omitting it retain
the original app-exit fallback. Failed native state/address/scan polls also mark
the radio closing, so timeout-cleanup errors cannot evade storage revocation.
Once a provider's storage is revoked, successful RF cleanup does not resurrect
that context. Cleanup does not depend on pending
SPI/display work. A failure never falsely drops ownership; retrying leave or
cancel may finish cleanup. No new join/scan is accepted in the meantime.

SDK control functions have internal task/mutex behavior that the public API
cannot interrupt. The adapter adds no blocking RF completion wait or unbounded
retry loop. The old header's 100ms leave objective is not hardware-qualified:
IDF stop/deinit/event-loop teardown latency has not been measured on target.
Returned SDK init failures are assumed to unwind per SDK contract; hidden
assertions/failed internal unwind cannot be certified by host fault injection.

## Verification

`bash test/run_radio_test.sh` covers the real native adapter under SDK fault
injection, actual CpuPort ownership/bounds, real JSON/manifest admission and the
actual Runtime/Graph with separate `dlopen` app/provider images. It tests absent
backends, malformed graph/config, unselected admission, exclusive/stale tokens,
copy/wipe behavior, scan/cancel/retry, failing cleanup stages, stale events,
Light/Deep barriers, a separate real bound-storage provider remaining live
during healthy scan/join but revoked after cleanup/poll failure, and no
app-fini/unload/handoff after active/retained radio.
`SANITIZE=1` adds ASan/UBSan. Leak sanitizer may be unavailable in ptrace-based
executors; any disable is recorded with test results, not called a leak check.

These checks do not operate a physical radio, join a network, enter real
credentials, prove electrical sleep current, measure native deadlines or qualify
the Watch. No hardware, flash, merge or release follows from a build/test pass.

Official SDK source contracts:
- [Wi-Fi API, IDF4.4.7](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_wifi/include/esp_wifi.h)
- [Default netif/handler lifecycle](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_wifi/src/wifi_default.c)
- [Event-loop lifecycle](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_event/default_event_loop.c)
- [Netif implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_netif/lwip/esp_netif_lwip.c)

## Credential transport scratch

The generic Runtime app/bound-KV read scratch and native NVS read/readback scratch
use a noncopyable scope guard that writes every owned byte through a volatile
pointer on every ordinary success/error exit. This covers transient transport
copies when an external application stores a credential-bearing blob. It does
not change the KV ABI, encrypt NVS, erase caller/SDK/flash copies, guarantee
register erasure, or claim crash-dump or physical-remanence protection.
`run_key_value_test.sh` directly checks the guard at optimization and runs the
existing Runtime/NVS failure matrix; ASan/UBSan verifies bounds.

Normal SDK logging for the reviewed Wi-Fi component tags is suppressed with
verified per-tag readback before SDK credential configuration. Original effective
levels are restored only after full cleanup; failed suppression/restoration is
retained and retryable. The native shim README lists the exact tags and limits.
This is not a guarantee about unreviewed supplicant, proprietary binary,
early/DRAM, ROM or direct-console output, and no physical UART privacy test has
been performed. No SDK-wide or NVS encryption claim is made.
