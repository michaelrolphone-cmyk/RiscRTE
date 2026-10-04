# Native radio SDK-shim regression

This test includes the production `NativeRadio.h`. The shim is deliberately
limited to the public calls it makes. It models operation failures, ownership,
credential copies, the destructive driver-clear behavior, and queued events; it
does not emulate a Wi-Fi stack, execute Xtensa instructions, or qualify hardware.

## Contracts

- All ordinary calls are serialized by the CPU owner. Only the native event
  callback runs concurrently; it touches generation-tagged 32-bit atomics only.
- Logical claim does not call the backend. Initialization is lazy on join/scan.
  Another initialized Wi-Fi owner or existing default event loop is rejected,
  never adopted. Arduino WiFi and other native networking owners cannot coexist.
- Only station mode is enabled. No AP interface is created. Credentials are
  copied into a local config, passed to RAM-only SDK storage, then wiped through
  volatile stores before starting. The SDK config is overwritten on leave.
  No application pointer or credential is retained by this adapter and it logs
  no credentials. Driver `nvs_enable` is false and storage is `WIFI_STORAGE_RAM`.
- `state` reports DOWN=0, JOINING=1, UP=2. UP requires both a current association
  and nonzero live IPv4 on an up netif. The 12 station address bytes are address,
  gateway, netmask, each four network-order octets; AP bytes remain zero.
- A station disconnect never reconnects. While the initial connection lacks
  IPv4, polling enforces a 30-second deadline. Timeout reports DOWN only after
  successful full cleanup; a cleanup error returns false and retains ownership.
  The deadline is poll-driven, not an extra task or timer.
- Scan starts with `block=false`, active per-channel dwell maximum 120 ms, and
  copies at most 16 records after the completion event. SSIDs are bounded to 32
  bytes plus a terminator. WPA/WPA2/WPA3 PSK modes are descriptive; WEP,
  enterprise and unknown authentication modes are marked unsupported.
- Polling bounds a scan to ten seconds. DONE and FAILED retain the native
  resources until explicit cancel/leave. A repeated start or join before
  successful cleanup fails. No SDK result pointer crosses the ABI.
- Cleanup revokes callback authority first, stops an attempted scan, frees its
  result list, disconnects a join, overwrites its RAM credentials, stops and
  deinitializes Wi-Fi, unregisters the observer, clears default driver/hooks,
  deletes the owned default event loop, stops DHCP, and destroys its netif.
  Each failed stage preserves enough state for a later retry and keeps `idle`
  false. Successful stages are not blindly rerun.
- Generation tags reject delayed calls to old observers. They do **not** alone
  reject old queued SDK events delivered to a new registration: full deletion
  of the owned default event loop drops the old queue before a retry can start.
  The adapter does not use cached IP events to declare a new connection UP.

## Pinned SDK sources reviewed

The local cache had the compiler but not the Arduino framework. These official
Arduino 2.0.17 ESP32-S3 headers were inspected through the public source mirror:

- [esp_wifi.h](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/include/esp_wifi/include/esp_wifi.h)
- [esp_wifi_types.h](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/include/esp_wifi/include/esp_wifi_types.h)
- [esp_netif.h](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/include/esp_netif/include/esp_netif.h)

Matching ESP-IDF 4.4.7 implementation details are important to cleanup:

- [wifi_default.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_wifi/src/wifi_default.c):
  use fallible `esp_netif_new` plus checked attachment/handler registration,
  rather than the assert-based `esp_netif_create_default_wifi_sta` helper.
  `esp_wifi_clear_default_wifi_driver_and_handlers` destroys the driver even
  when its configuration-clear call returns an error. The adapter records that
  destruction, retains the netif, and retries only `esp_netif_set_driver_config`
  with an empty config. Repeating the destructive helper could double-free.
- [wifi_netif.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_wifi/src/wifi_netif.c):
  no-driver attachment allocation failure is safe to clear: destruction accepts
  a null driver. The regression separately models this path and an attachment
  failure after the driver handle exists.
- [esp_netif_lwip.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_netif/lwip/esp_netif_lwip.c):
  attach retains the driver handle before its post-attach callback; DHCP-stop
  is checked before the public netif-stop action and void destroy operation.
- [esp_event.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_event/esp_event.c)
  and [default_event_loop.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_event/default_event_loop.c):
  default-loop creation rejects a preexisting owner; successful loop deletion
  removes registrations and queued event copies. Unregistration is synchronized
  with active handler dispatch.
- [wifi_init.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_wifi/src/wifi_init.c):
  initializing an already-initialized driver returns success, hence the explicit
  NOT_INIT preflight. Returned initialization failures attempt SDK-internal
  unwind. An internal unwind failure hidden by that API is not independently
  observable or recoverable through this adapter; the shim cannot qualify it.

## Limits of the evidence

`esp_netif_deinit` is explicitly unsupported in IDF 4.4. Its once-initialized
TCP/IP infrastructure remains for the boot session. `idle` means all owned
radio, interface, driver, event task/queue and callback resources are released;
it does not assert destruction of the SDK-global TCP/IP infrastructure. Internal
SDK allocation-assert paths, hidden internal cleanup failures, RF behavior,
DHCP reliability, SDK synchronous-call latency, and physical sleep current are
outside this host regression. The shared hardware header's 100-ms call objective
is not an enforceable wall-clock guarantee for synchronous IDF init/start/stop,
netif IPC, event-loop unregistration, or cleanup calls: the SDK exposes no
timeout/cancellation argument for those calls. Asynchronous RF completion and
bounded copied results do not make those control calls interruptible. The 16-record bound applies to copied public
results, not the SDK's private scan allocation.

The regression covers validation without I/O; lazy/exclusive initialization;
all initialization and cleanup fault stages; failed initialization plus failed
cleanup; caller credential mutation and local config wipe before start; IP wire
order; no reconnect; timeout cleanup and cleanup failure; empty/large/failed
scans; scan result copying/auth conversion; completion during start; cancellation
and retry with queued/stale events; and idempotent final release.

Standalone host invocation:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/native_radio_shim -Isdk/driver -Isrc \
  test/native_radio_test.cpp -o /tmp/native-radio-test
/tmp/native-radio-test
```

ASan/UBSan can use the same command with `-fsanitize=address,undefined
-fno-sanitize-recover=all -fno-omit-frame-pointer -g`. If this executor is running
under ptrace, LeakSanitizer cannot run; `ASAN_OPTIONS=detect_leaks=0` permits
ASan/UBSan checks but must not be reported as a LeakSanitizer pass.

## Scoped SDK log suppression

The adapter snapshots the observed levels of `wifi`, `wifi_init`,
`wifi_init_default`, and `wifi_netif`, sets only those tags to `ESP_LOG_NONE`,
and verifies readback before Wi-Fi initialization. It rechecks suppression
before credential configuration, start/connect/scan, and active-radio cleanup.
Initialization/configuration changes to a tag are muted again without replacing
the original snapshot. Suppression failure before configuration prevents the
SSID/password copy reaching the SDK; failure to re-establish suppression for an
active radio retains ownership for retry. Prior levels are restored only after
radio, netif, event loop and handlers are gone. A failed restoration remains
pending and blocks a new operation. Unrelated tags and the global output sink
are unchanged; other components must not concurrently change these owned tags.
The effective levels are restored, not the former implicit wildcard-inheritance
relationship, for which the API provides no tag-removal operation.

Supported public API evidence is IDF 4.4.7
[`esp_log.h`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/log/include/esp_log.h)
(`esp_log_level_get` and void `esp_log_level_set`) and
[`log.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/log/log.c).
The setter can silently fail to allocate its per-tag entry, hence the readback
checks and fault tests. The exact Arduino 2.0.17 logging header could not be
retrieved in this environment; compilation against the pinned firmware SDK
remains a required target check. The Wi-Fi tag names are evidenced in the
reviewed `wifi_init.c`, `wifi_default.c`, and `wifi_netif.c` sources above.

This suppresses normal ESP logging through the listed tags, including their
error-level messages. It is not proof that every SDK diagnostic path is silent:
per-tag filtering does not cover `ESP_EARLY_LOG*`/`ESP_DRAM_LOG*`, direct ROM or
console writes, unreviewed supplicant tags, or all behavior inside proprietary
Wi-Fi binaries. The exact pinned supplicant implementation was unavailable to
this source review. Host tests model the filter lifecycle and mutation/failure
cases; they do not qualify physical UART privacy or all SDK-internal logging.

## Scan-session allocation

The copied scan cache is a single 600-byte
[`heap_caps_calloc`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/heap/include/esp_heap_caps.h) allocation with
`MALLOC_CAP_8BIT`. No SDK or log calls occur when that allocation fails. Join-only
and idle states do not allocate the cache. It is assigned to native state only
after initialization succeeds, remains owned through scanning/results and failed
cleanup, and is volatile-wiped before exactly one `heap_caps_free` on successful
quiescence. A failed begin frees the unpublished cache, including if separate
SDK cleanup remains retained. No app-ledger pointer or event callback references
this memory. Tests inject OOM and retry, failed begin/start, result-copy stability,
every cleanup fault and log-restoration failure; frees assert zeroed bytes.

Pinned Xtensa GCC 8.4 size-only measurement using the shim declarations changed
State from 664 to 72 bytes (592 static bytes recovered). The host suite bounds its
control-state size as a regression guard. This is not proof of target runtime
heap capacity; full target linking and physical qualification remain separate.
