# Bounded integrated BLE HCI controller

Firmware 0.1.24 supplies `platform.hci.controller@1` for the exact selected
`espressif,esp32s3-ble` / `radio.integrated@1` device, unit 0, features 1.
The existing `twatch_hci_controller_v1` layout is unchanged. Merely declaring a
board device does not grant it: the selected provider must require the raw
capability. The entire graph is validated before any controller operation.
There is no Bluetooth host stack, pairing, advertising, discovery, saved key,
credential, product preference or automatic controller activation in firmware.

## Ownership and lifecycle

The CPU port gives the selected provider one generation-tagged session token.
All entry points require the runtime owner task; the native controller owns its
callbacks and packet buffers. `open` explicitly initializes and enables BLE;
`close` disables then deinitializes it. A provider can defer `open` until an
external application requests it. The native callback functions live permanently
in firmware and never refer to an app or dynamically loaded provider function.

Healthy HCI survives ordinary app handoffs. Its packet buffers and session belong
to the persistent provider/native owner, not the app that happened to enable it.
The provider must not retain app pointers. Active HCI prevents Light/Deep sleep
and bank restart. External sleep policy must explicitly close it before sleeping
and may reopen it afterward; deep wake starts a new boot. Runtime performs no
implicit stop/restart on navigation or sleep.

Failed native packet operations become cleanup-only. A malformed/oversized
controller packet or full receive queue latches a stream fault instead of silently
losing HCI state. A failed close retains the session and buffers; close can be
retried if the SDK state still matches owned resources. Failed `open` first tries
cleanup. If cleanup cannot prove idle, `open` returns false **with a nonzero
cleanup token**, which the provider must retain. Failed/uncertain HCI blocks app
finalization/unload, provider storage, sleep and restart. Invalid input or a
stale/wrong-owner token is rejected before native I/O without poisoning a healthy
session. Closing successfully invalidates the token and empties packet storage.

There is a deliberately conservative special case: an ESP-IDF controller-init
error is retained until restart. The pinned source has early failure paths after
PHY/power initialization but before its status becomes INITED. An IDLE status
alone cannot distinguish a complete rollback from those paths, and the public
`deinit` API refuses an IDLE controller. Runtime does not claim that hardware is
off or free those uncertain resources.

## Non-consuming status extension

`RiscHciControllerStatusV1.h` appends one callback after the unchanged raw
transport prefix. It uses the same capability/version; check `struct_size`
against `sizeof(risc_hci_controller_status_v1)` before using it. `status` accepts
an exact live token and reports Off (0), On (1) or Retained (2). A zero token
succeeds with Off only when neither a logical session nor native resources
remain. Wrong-owner/stale-token or otherwise rejected queries return false and
initialize the result to Retained. Status never consumes/discards events,
changes controller power or performs cleanup; detecting an asynchronous fault
makes the matching session cleanup-only. Older providers keep the exact prefix.

## Packet bounds

- One session, 4,124 receive bytes including a three-byte header per packet.
  Four maximum-size ACL packets fit, as do larger bursts of short events.
  FIFO ordering and packet boundaries are preserved; true byte exhaustion
  faults the session. See [the 0.1.73 burst regression](HCI_RX_BURSTS.md).
- H4 packet type is separate from the public payload. TX accepts command type 1
  (3–258 bytes) or ACL type 2 (4–1028 bytes); RX accepts event type 4 (2–257 bytes)
  or ACL type 2. Every standard HCI length field must exactly match the payload.
- Receive requires at least 1028 bytes of caller capacity. A healthy empty poll
  returns true with length 0. A fault returns false with length 0.
- Send/receive waits accept 0–20 ms. Waiting yields one RTOS tick between checks;
  scheduling/tick granularity can extend observed wall time beyond the requested
  deadline. There is no queued caller TX pointer; the controller receives a
  bounded native-owned copy.
- Queue/TX storage (about 5 KiB) is allocated from internal 8-bit heap only when
  opened. No PSRAM callback/DMA buffer or fall-back allocation is used.
- Controller modem sleep is disabled for this transport. Generic runtime sleep
  still requires close. This avoids IDF's modem-wakeup semaphore in VHCI send.
  SDK init/enable/disable/deinit calls are synchronous and have no cancel/deadline
  API; this implementation does not pretend to preempt a stuck SDK call.

## Source checks and verification

The native code is original integration code. SDK declarations and failure paths
were checked against [ESP-IDF v4.4.7 ESP32-C3/S3 controller source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/controller/esp32c3/bt.c),
[its public header](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/include/esp32c3/include/esp_bt.h),
and [the official VHCI example](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/bluetooth/hci/controller_vhci_ble_adv/main/app_bt.c).
The Arduino 2.0.17 S3 `btInUse()` default remains true, preserving controller
memory for later explicit open; Runtime does not release it irreversibly.
See [Arduino startup](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-misc.c)
and [Bluetooth HAL](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-bt.c).

`bash test/run_hci_test.sh` covers config/manifest admission without hardware I/O,
owner/token/packet bounds, healthy app handoffs, Light/Deep and restart barriers,
failed-open/close handling and SDK queue/failure models. Its real host
Runtime/Graph/dlopen fixture proves default-child-default mapping lifetime with
Bluetooth left enabled and retention before app fini after uncertainty.
`SANITIZE=1` enables ASan/UBSan; on ptrace-managed executors only, set
`ASAN_OPTIONS=detect_leaks=0` if LeakSanitizer cannot run. CI uses its normal
sanitizer defaults. Host shims model SDK APIs; they do not prove RF, pairing,
physical power state, coexistence, wake reliability or current consumption.
No hardware qualification is claimed.

## Generic native-USB metadata allocation

The generic `esp32s3-16mb-usb` target now explicitly selects the same retained,
PSRAM-only Runtime metadata allocator already used by the paired target. Adding
the controller exposed a 2464-byte internal-DRAM link overflow on generic USB;
the baseline and separately built paired firmware had passed. Runtime metadata
is owner-task-only configuration, not an ISR/DMA/controller packet buffer.
This relocation preserves all manifest, capability, key and graph limits and
retains the allocation until reset, including failed quiescence. Missing PSRAM
or allocation failure stops before bootfs mounting or driver activation, without
falling back to internal heap. The paired path and baseline/embedded targets keep
their existing allocation choices. Controller packet buffers remain internal.
