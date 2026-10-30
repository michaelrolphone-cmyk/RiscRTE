# Native platform console host verification

Run from the Runtime root:

```sh
./test/run_native_platform_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./test/run_native_platform_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./test/run_native_console_records_test.sh
```

`IDF_PATH` defaults to the sibling `../esp-idf` checkout. These tests compile the
actual `NativePlatform.cpp`, independently for USB Serial/JTAG and UART, against
ESP-IDF's real public driver headers. The selected checkout is ESP-IDF v6.1.
The SDK error constants, UART HAL types and ESP32-S3 capabilities are also real.
The shims replace FreeRTOS, timer, heap and logging declarations only. The driver
function bodies are deterministic mocks, so no host test accesses a device.

Coverage:

- An uninstalled console does no driver I/O and reports no available capacity.
- Successful installation captures ownership, default buffer configuration and
  the owning core; repeated begin/end and reinstallation are handled.
- An already installed foreign driver, including an install-race rejection, is
  neither adopted nor uninstalled.
- Installation failure leaves the console unowned and supports a later retry.
- Copy and move construction/assignment are forbidden at compile time.
- TX buffer configuration honors lower/upper bounds and cannot mutate a live
  instance; failed configuration preserves the last accepted size.
- Every native USB build defaults to an 8192-byte TX buffer, independent of
  stage diagnostics. Whole-record capacity is distinct from the 128-byte write
  budget. Accepted bytes consume credits; only the public SDK's zero-timeout
  complete-drain query reclaims them. Partial drain conservatively retains
  credits. Wrong-core and uninstalled callers receive zero capacity.
- The actual Console and SleepDiagnostics line sink emit complete 127, 128,
  129, 200 and 255-character records with stage logs both off and on. An 8 KiB
  burst fills the buffer; full-buffer admission drops a whole record, and host
  reconnection alone never fabricates space. Completion restores capacity.
- Each write is bounded to 128 bytes. USB calls use timeout zero even after a
  nonzero timeout request. Both transports reject wrong-core writes. Null and
  empty inputs perform no SDK write.
- Partial, zero, negative and impossible overlength SDK returns are handled;
  println stops on failed progress and counts accepted bytes/newline accurately.
- RX reads use one byte and zero timeout. Repeated availability checks preserve
  the cache, including NUL and 0xff. Errors do not fabricate data.
- Wrong-core teardown performs no uninstall; an injected uninstall error retains
  ownership, the RX cache and buffer settings until a successful same-core retry.
- Successful teardown clears cached input before reinstalling.
- Monotonic microsecond conversion floors sub-millisecond values, wraps at 2^32
  milliseconds, and supports unsigned elapsed-time subtraction across wrap.
- Free heap forwarding and scheduler delays are checked at a mocked 100 Hz tick
  rate, including requests smaller than one tick.
- UART-only configuration errors do not install or own a driver. UART0, 8N1,
  disabled flow control, source clock, RX buffer and queue settings are verified.

## Verification limits

These are contract and error-injection tests, not hardware qualification. They
do not execute the real SDK interrupt/ring-buffer implementation, test physical
USB connectivity, prove concurrent accesses safe, or establish device timing.
The selected native target pins its owner main task to CPU1; recording the
installation core relies on that non-migrating owner task.

The USB write implementation passes zero timeout to both SDK synchronization
operations. In contrast, ESP-IDF v6.1's `uart_tx_chars()` internally takes its TX
mutex with `portMAX_DELAY` before writing the FIFO. The UART host test proves
bounded payloads and use of the FIFO entry point, but cannot claim a strictly
nonblocking UART call under contention. The selected native USB target is not
affected by this UART caveat.

The uninstall-error test is a defensive fault-injection contract. The selected
SDK's USB uninstall implementation currently returns `ESP_OK` and does not
propagate `esp_intr_free()` failure. This host test therefore cannot prove cleanup
when an actual SDK interrupt release fails.

In traced host environments LeakSanitizer may fail with its documented ptrace
restriction. The sanitizer command above disables leak detection only; address
and undefined-behavior instrumentation remain enabled.
