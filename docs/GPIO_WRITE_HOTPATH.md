# Scoped GPIO write hot path (Runtime 0.1.45)

The ordinary X4 UC8279 provider sends two 600-row, 800-pixel planes through its
existing typed GPIO scope. The payload is 120,000 bytes and its bit loop issues
one MOSI write plus two clock writes per bit: **2,880,000 scoped writes**. Command,
control, probe and scheduler operations are additional and are not counted here.
No driver, ABI, controller timing, pin authority or raw MMIO import is changed.

## Work removed

At baseline `bdd389b00146f4b090acd08334b393168af49621` (production runtime
`57d3b9d`, version 0.1.44), each write scans `pins_` until its exact scope/token:

- GPIO11 MOSI: 960,000 writes times 12 pin candidates = 11,520,000 candidates.
- GPIO12 clock: 1,920,000 writes times 13 = 24,960,000 candidates.
- Total: **36,480,000 pin candidates** for payload only.
- Native `stopPwm` scans four channels each time: **11,520,000 slot checks**.

A 64-byte direct-mapped cache now stores only pin-index hints (`index + 1`),
indexed by the opaque token's low six bits. The full current token and scope
must match before using a hint. Both output mode and hold state are checked
again before hardware access. The original owner/poison/sleep checks still run
on every call. A collision falls back to the bounded 49-pin scan and refreshes
the hint; it can hurt performance, never grant authority. Tokens retain the same
monotonic allocation, opacity, lifetime and exhaustion behavior. Release and
retirement need no cache invalidation because the authoritative pin record is
always checked. There is no heap allocation or asynchronous operation.

For two noncolliding payload tokens the warm path checks **one pin candidate
per call**: 2,880,000 candidates, a 12.67-fold reduction in candidate inspections.
An initially empty cache adds 23 candidates over that total across the first
MOSI and clock writes. Deliberately colliding tokens still work but do not get
this hot-path reduction. This is not a worst-case constant-time lookup claim.

Native PWM adds 49 bytes of per-pin ownership flags. An unowned pin returns
immediately from `stopPwm`, even while a different pin owns PWM. The flag is set
before channel configuration so failed partial configuration remains owned.
Failed `ledc_stop` does not clear it. Only successful cleanup clears ownership.
Timer failures preserve existing ownership exactly as before. The original
four-channel cleanup, output-signal disconnect and static GPIO write remain for
owned pins. No memory, task or lifecycle barriers are weakened.

## Reproducible host timing

Run `bash test/run_scoped_gpio_benchmark.sh [baseline-ref]`. It compiles the
baseline's actual CpuPort and native stop function from Git, then the current
ones, at `-O2` without LTO. It calls the scoped write table for the exact payload
sequence. Native SDK writes are a counted host stub; no physical GPIO or display
is exercised. Both no-PWM and four-unrelated-PWM cases run nine frames each.

One x86_64 Linux/GCC 14 host run:

| Native state | Baseline median | Current median | Ratio |
| --- | ---: | ---: | ---: |
| No PWM channels | 29.271 ms | 14.600 ms | 2.00x |
| Four unrelated channels | 31.450 ms | 15.320 ms | 2.05x |

These are informational software timings, not a CI timing threshold or an
ESP32-S3/X4 throughput prediction. SDK GPIO cost, PSRAM placement, scheduling,
probe delays, refresh waveform and BUSY wait remain outside this measurement.
The number of physical transitions, owner checks and native GPIO calls does
not change. Hardware throughput and panel behavior require a separate device
retest; none is performed by these checks.

## Regression coverage

`test/run_held_output_test.sh` runs the cache regression alongside existing real
board/graph held-output and touch tests, plus the actual native PWM cleanup
implementation under a focused SDK shim. CI already runs it normally and with
ASan/UBSan. The new cases cover same-bucket tokens, high token bits, foreign
scopes, stale tokens, input reclaims, all 49 pins, task ownership, poisoned and
sleeping states, held/retired output revocation, successful reclaim, exhaustion,
failed release, failed PWM, failed static writes and hold eligibility.

Native PWM coverage retains all 1,023 intermediate duty ratios, exact endpoints
and the Watch 40/100 duty mapping. Added checks cover unrelated active PWM,
failed timer/channel/stop setup, failure after cleanup, full-channel capacity,
slot reuse and successful cleanup of every occupied channel.
