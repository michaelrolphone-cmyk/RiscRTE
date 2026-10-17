# Native serial storage admission

Baseline: Runtime `070d5c7e347f74357f2e8843312409315aca5762`, Waveshare producer `aa0664c2905abb264a6b07f35d027b4879562c68`, shared Watch `b00d4e7d36fc69c8defd250a82f6af077c732137`.

The actual Runtime/CpuPort/NativeSettingsLifecycle and Waveshare SD/FatFs host composition reproduced this sequence:

1. Native serial HELLO, namespace enumeration and PATH succeed.
2. Native settings discovery owns an SD session and a native storage pin. A serial STAT is incorrectly dispatched and returns clean `RISC_FILES_UNAVAILABLE` (-3).
3. The copied SD session closes while native publication still holds its pin. The provider's public state is READY, but owner-thread I/O remains forbidden. Another incorrectly dispatched STAT returns `RISC_FILES_RETAINED` (-6), revokes app authority and produces `serial file custody retained; restart required`.
4. A control containing only the first -3 followed by immediate app return exits cleanly. The unavailable reply alone does not prove retention.

`Port::serialFilesReady` is a native-only, read-only dispatch gate. The ESP32-S3 composition supplies `NativeSettingsLifecycle::exitSafe`. Runtime reports checkpoint BUSY without consuming the request while that gate is false. The same queued STAT runs once after native custody releases. This does not change the public RTEF1 interface, provider state semantics or application permissions.

A separate diagnostic records the first failing app-exit predicate in the existing final retention message. It preserves evaluation order, short-circuiting, the first existing error and provider-graph diagnostic priority. It adds no extra callback reads, polling or cleanup.

## Qualification

- `test/run_serial_native_provider_test.sh`: 15 scenarios pass normally and with ASan/UBSan. New cases prove deferred requests retain app authority, execute after readiness, cancel pending requests on disconnect while deferred, and identify a native-port exit refusal.
- The product's complete source-bound native scheduler composition passes continued and immediate-return cases under ASan/UBSan. The queued STAT returns success with the expected four-byte file after active-session and closed-session intervals. Runtime, native lifecycle and serial client all finish without retention.
- Product provider/Home boundary tests: 14 normal and sanitized cases, published in Waveshare PR17. They distinguish clean unavailability, later custody loss and Home continuation.
- LeakSanitizer is disabled for the focused Runtime runner because this execution container reports that it cannot run under ptrace. AddressSanitizer and UndefinedBehaviorSanitizer pass; no leak-check claim is made.

Hardware is not qualified by these host results. The reported 0.1.57 hardware log has a clean STAT -3 and later **generic** native retention barrier, with 154 dropped log records. The reproduced serial-retention diagnostic differs. The new first-predicate diagnostic is intended to identify any remaining branch; this patch does not claim that every observed hardware failure is explained.

The fixture models RTOS scheduling, vendor NVS and SD sector transport. Actual provider, FatFs, Runtime graph, native settings lifecycle and ownership code are exercised. No physical card, USB reconnection or real-device transfer was performed by these tests.
