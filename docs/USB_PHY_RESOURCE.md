# Optional internal USB PHY ownership

Runtime 0.1.80 can materialize provider-only `platform.usb.phy.resource@1` when a composed native build explicitly selects `RISC_ENABLE_USB_PHY=1`. The exact shared header is `sdk/driver/RiscUsbPhyResourceV1.h`. The current backend requires ESP32-S3 HWCDC mode (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`); ordinary targets remain disabled.

The native layer owns the lease, console exclusion and lifecycle barriers. USB descriptors, endpoints, stack state, mass-storage/SCSI and SD ownership remain in ordinary providers. The provider must stop all controller callbacks, transfers and DMA and restore its PHY routing before releasing the lease.

Claim checks the owner task and generation, reserves DM19/DP20 against native GPIO/bus claims, and fences diagnostic USB output/recovery before ending HWCDC. RAM diagnostic capture remains available, while native storage drains are suppressed. Release uses a bounded raw 20 ms detach interval and checks the restored RX/TX resources without waiting for a host. A clean refusal returns token zero. A partial claim or failed release preserves token, mappings, dependencies and the native fence. A matching owner release can retry cleanup. Unknown/stale tokens never reach native callbacks.

The [source-only Runtime 0.1.99 forward-port](USB_SERIAL_RESTORATION_0199.md) of
the independent 0.1.87 repair enables and resets the native USB-Serial-JTAG module, disables
its pads through the detach interval, and selects its internal PHY using the
pinned IDF low-level sequence before recreating HWCDC. It checks module clock/reset,
the RTC controller selection, the internal-PHY selector, pullup and pad readbacks
as well as RX/TX allocation. Failed recreation tears down partial HWCDC resources
and retains the native fence. These operations happen only after the provider's
checked stop and media return; no USB protocol, SD transport, CPU restart or
app launch is added to Runtime.

The old adapter could return success with live queues while the native hardware
was unselected or disabled. The expanded host fixture reproduces that false
success and validates absent-host recovery, repeated release/reclaim, RX/TX after
reattachment and stuck clock/reset/route/pad failures. The real Runtime/Graph/Port
dlopen lifecycle now uses this pinned HWCDC adapter instead of a boolean native
stand-in. That proves the checked software boundary, not the physical cause of
the reported missing serial or apparent restart on cable removal. Enumeration
and reset-cause validation remain unperformed hardware work.

The route register operations follow the pinned ESP-IDF 4.4.7
[`usb_phy_ll_int_jtag_enable`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32s3/include/hal/usb_phy_ll.h)
sequence and use the
[`usb_serial_jtag_ll` module/pad helpers](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32s3/include/hal/usb_serial_jtag_ll.h).
The host register shims model their checked boundary and injected failures;
HWCDC itself remains the unmodified pinned Arduino source.

A held lease refuses app exit, restart and all ordinary/timed/wake-set/deep-hold sleep forms. A failed cleanup reports retained sleep state. It deliberately leaves provider storage safety and owner SD/SPI operations available so an MSC provider can finish block I/O and remount its card. The SD provider must separately exclude filesystem clients and pause logging during export. No automatic SD formatting, endpoint implementation or product action exists here.

The native hardware table appends `usbPhyIdle`, `usbPhySuspend`, and `usbPhyResume` callbacks. `CpuPort::bind` registers the readonly API only when all three are supplied. Registration and full-cohort validation do not call them, so the same materializer works for ordinary boot, copied native-first admission and provisioning metadata. A missing backend fails closed; ordinary apps cannot acquire the raw platform capability. The readonly ELF symbol `risc_usb_phy_resource_enabled` contains uint32 value 1 only in an enabled native build. A candidate validator should check its actual value, readonly section and selected implementation symbols before supplying inert metadata-only callbacks.

`bash test/run_usb_phy_test.sh` exercises the production Port and Runtime graph: ownership/generation/pad exclusion, zero/nonzero-token refusal, checked release retry, all sleep forms, continuing owner SPI transfer, exact table validation, native-first admission and real app/provider retention across dlopen lifecycles. Its HWCDC test executes the pinned Arduino 2.0.17 driver with diagnostics/stage flags off/on, including pending wake recovery, every allocation failure, RAM trace capture and suppressed SD drains. Use `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` for ASan/UBSan. Host and native builds do not establish physical enumeration, throughput, SD consistency or cable-removal detection.
