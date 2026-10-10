# Pinned HWCDC host regression

The `vendor/HWCDC.cpp` and `vendor/HWCDC.h` files are byte-exact copies from
Espressif Arduino ESP32 2.0.17, resolved to commit
`5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2` on 2026-10-07. The tagged source
was compared with the immutable-commit source before inclusion. Both files
retain Espressif's Apache-2.0 notices. The full applicable license is included
as `vendor/LICENSE-APACHE-2.0.txt`.

- https://github.com/espressif/arduino-esp32/blob/5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2/cores/esp32/HWCDC.cpp
- https://github.com/espressif/arduino-esp32/blob/5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2/cores/esp32/HWCDC.h
- `HWCDC.cpp`: SHA-256 `d0a8ca606c2729c8522a041113285dbf27033c22a5a6af8649a7305ffe84c449`, Git blob `8d4392544b0771bf1d5dddc8e2fde60698eff5fa`
- `HWCDC.h`: SHA-256 `6bd38592b50b53a8a9ca750218128697c1cb1143bd8744aa365150f4d368b8f3`, Git blob `734e0cd5888acc83a9b5796f331854a989d89405`

The runner checks both SHA-256 digests before compiling, requires no network,
and links the actual HWCDC implementation against small host replacements for
FreeRTOS, GPIO, interrupts and USB registers. It also links the real production
SleepDiagnostics adapter, with the recorder enabled and disabled. The actual
upstream C++ file is included in the test translation unit so the test can
observe its static state and inject host status deterministically. It is not
rewritten or a simplified replacement of begin/end.

Covered paths:

- Public begin/end with successful allocation and injected failure of each RX
  queue, TX ring, mutex and interrupt allocation
- The production public-method readiness check detecting every such failure
- Exact interrupt-failure cleanup and complete partial-allocation teardown
- Deferred, one-shot owner-polled end/begin and a 20ms detach period
- Failed recovery remaining offline without automatic initialization retries;
  a later successful Light return permits one bounded attempt
- Native callbacks touching no USB before poll, absent host, rejected Light
  return, wrong owner, and no positive semaphore/queue wait or delay call
- Actual RX interrupt and queue driving diagnostic replay after recovery

`shim/sdk.h` is a deterministic host model, not an ESP32 emulator. Register
assignments and GPIO calls are observed, but real RTOS concurrency, electrical
USB detach, host enumeration timing and silicon power behavior are unqualified.
The mutex and ring operations model only the operations needed by these tests.
They do not establish the absence of every race or fault in upstream HWCDC.

The runtime assumes a sole owner/producer and does not register HWCDC event
callbacks or enable its debug output. Testing optional upstream event-loop
creation/deletion or concurrent external users is outside this adapter's scope.
The fixture does not become firmware code and never runs on a serial device.

The USB ownership suite (`test/run_usb_phy_test.sh`) additionally models the
Serial/JTAG module clock/reset and RTC internal-PHY selection. It executes the
production native resume after checked provider release, including disabled or
unselected hardware, stuck register failures, all allocation failures, and 16
absent-host release/reclaim cycles followed by real pinned-driver RX/TX.
`usb_phy_lifecycle_bridge.cpp` links the same native adapter into the actual
Runtime/Graph/Port/dlopen retention test. The before/after regression deliberately
sets an unselected, clock-gated backend; it does not establish that a particular
physical USB controller failure or cable event creates that state on a device.

Run `bash run.sh /path/to/runtime`; add `SANITIZE=1` for ASan/UBSan.
The stage-build cases also execute the unmodified pinned driver with 8192-byte
preallocation, an undrained roughly 6 KiB startup burst, a completely full ring,
missing host, allocation fallback and recovery-capacity restoration. Ordinary
cases assert the original 256-byte capacity. These tests observe bounded ring
capacity and zero waits; they do not promise delivery of arbitrary-sized bursts.
Under process tracing, use `ASAN_OPTIONS=detect_leaks=0` if LeakSanitizer cannot
run. This test explicitly counts stub resource allocation/deallocation too.
