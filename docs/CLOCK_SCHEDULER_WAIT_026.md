# Runtime 0.2.6: optional scheduler-only clock wait

This isolated successor starts from Runtime 0.2.5 commit
`1be501bd69b0ab21a3acaeacaa382796f284e0e1`. It preserves its HCI/Wi-Fi shared
lease, the full 0.2.4 capacity selection, and every existing clock prefix field.
The version reservation follows a fresh live branch/tag/open-PR collision check
recorded in `evidence/clock-scheduler-wait/version-reservation.json`.

## Why a separate callback

Native `Hardware.sleep`, used by legacy `platform.clock.sleep_ms`, may poll
diagnostics. That is not a scheduler-only wait and must not be substituted for
one when a provider is retrying checked cleanup with uncertain resource custody.
The legacy callback and its 5000 ms clamp are unchanged.

`RiscPlatformClockWaitV1.h` supplies an append-only suffix on `platform.clock@1`:

```c
typedef struct {
    risc_platform_clock_api_v1 base;
    uint32_t wait_tag;       /* RISC_PLATFORM_CLOCK_WAIT_TAG_V1, 0x43575431 */
    uint32_t wait_version;   /* RISC_PLATFORM_CLOCK_WAIT_VERSION_V1, 1 */
    bool (*scheduler_wait_ms)(void *context, uint32_t milliseconds);
} risc_platform_clock_wait_v1;
```

The base `risc_platform_clock_api_v1` header is unchanged. CpuPort advertises the
whole suffix size only when the appended optional `Hardware.schedulerWait`
callback exists. Otherwise it exposes exactly the old prefix size and leaves
the suffix unpopulated. Existing prefix consumers can continue calling the
same clock methods. New consumers must validate before reading suffix fields;
`risc_platform_clock_wait_from_v1` checks pointer, base version, complete size,
exact tag/version and nonnull callback in that order.

A validated caller invokes `scheduler_wait_ms(base.context, milliseconds)`.
Requests of 1 through 50 ms perform exactly one scheduler delay, rounded up to
RTOS ticks with a minimum of one tick. True means that wait completed. Zero,
values greater than 50, wrong owner, ISR entry, or missing native ownership
return false without a delay. Tick rounding and RTOS scheduling mean this is
not a strict wall-clock completion deadline.

CpuPort and the native implementation both check ownership. The native body in
`NativeSchedulerWait.inc` calls `vTaskDelay(cooperativeDelayTicks(...))` directly.
It invokes no diagnostic poll, performance recorder, provider poll, storage,
radio, USB, or legacy sleep callback. The same production include also selects
the Hardware callback and is executed by the host fixture.

The wait is permitted during retained cleanup. It does not inspect or clear
poison, cleanup tokens, radio operations or transfer flags, and it does not
restore permission to use other APIs. Consumers choose their bounded retry
policy; a missing suffix must not silently fall back to legacy `sleep_ms`.
The provider continues to own custody after incomplete cleanup.

## Qualification

`test/run_clock_scheduler_wait_test.sh` binds real CpuPort tables into the real
Runtime registry and calls the production native wait and selection functions.
A separately compiled C consumer includes only the legacy header. Another C
consumer validates and calls the new suffix. Cases cover prefix-only and
selected tables, malformed sizes/tags/versions/callbacks, an exact-size heap
prefix (ASan overread guard), legacy 5000 ms clamp, owner/foreign-thread/ISR
rejection, native owner loss, zero/over-limit bounds, 1..50 ms waits at 100 and
1000 Hz, unchanged retained custody, and absence of other native work.

The suite runs normally, with ASan/UBSan, and with TSan. The local traced executor
cannot run LeakSanitizer, so its ASan/UBSan command uses
`ASAN_OPTIONS=detect_leaks=0`. Existing HCI/Wi-Fi, legacy HCI and Runtime capacity
regressions also run. The original clock header remains byte-for-byte unchanged.

The single-job final target builds use the committed source identity and the
same pinned framework/toolchain plus previously qualified local esptool 4.11.0
substitution used for 0.2.5. Ordinary and capacity29 IQ/TCP/entropy builds retain
the linked capacity markers, TCP/entropy selection and mandatory 64 KiB IQ
reservation/alias guard. Separate final receipts bind ELF/firmware hashes to
the source checkpoint. Host tests and target builds do not qualify scheduling
latency, RF coexistence, dynamic heap/stack use or cleanup on physical hardware.
No product selection, publication or device action is included.
