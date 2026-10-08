# Retained invocation cooperation (Runtime 0.1.71)

This isolated correction starts at the exact Runtime 0.1.69 checkpoint
`f3fcd8c958a252c923c44784c2546078042422ee`. Runtime 0.1.70 belongs to the
separate provisioning work. The 0.1.71 reservation was coordinated after live
firmware-tag and open-PR version checks on 2026-10-08. This branch does not
contain the 0.1.70 changes and is not a combined release.

## Reachable failure

The test-only characterization commit `bfe758c` loads actual host app/provider
images through production Runtime and GraphV2. The mapped app caches its Runtime
API at init, acquires an admitted provider, queues a child, and calls the
supported `retain_invocation` callback while still on its init stack. That
callback preserves the stack, images and allocations but sets `active_` false.

Running `RETAINED_YIELD_SCENARIOS=signal-init bash test/run_retained_yield_test.sh`
at that characterization commit fails after a bounded sample of nine cached
`yield_ms` calls. It reports zero ordinary delays, zero raw delays and zero
provider polls. The previous `!active()` early return means an app's legacy
infinite retained loop can spin without reaching the scheduler.

## Current owner, raw scheduler only

`Runtime::yield` now checks the existing current Runtime and owner, rejects
promotion, stream-busy, provider lifecycle/poll and recursive yield contexts,
and accepts only an active or already-retained invocation. The existing graph
retention check can also fence the invocation during an ordinary yield; that
same call then uses the retained path. A pending graph release cannot poll
providers because GraphV2 already rejects unsafe dependency reads.

The retained path clamps the request to 1..50ms and invokes only the optional,
compiled-in `Port::retainedDelay`. The ESP32-S3 composition wires this to the
same existing cooperative tick conversion and `vTaskDelay`, with no diagnostic
polling. Ordinary `Port::delay` keeps its previous diagnostic behavior. A port
without the explicit raw callback does nothing in the retained path; it never
falls back to a callback that may perform ordinary work.

No retained yield polls providers, retries cleanup, unloads code, calls fini,
frees the invocation, launches the queued child or reactivates authority. All
previous retention and queued-handoff suppression remain in force. Cached
capability addresses remain native pointers; this is not memory isolation.
Apps should still return promptly after retention and avoid all provider calls.
The scheduler escape accommodates older retained loops; it does not resume
normal app execution or make the retained invocation recoverable.

After `Runtime::run` returns, neither a saved callback nor a direct call on the
old Runtime can delay. A different Runtime object cannot borrow the active
session. The public app/provider ABI layouts and capability policy are unchanged;
only the private compiled-in port gains an optional callback.

## Native-only limitation

Ordinary yield does not call the native exit barrier. `appExitSafe` can be false
for healthy active radio/I2S, wake configuration or other temporary ownership;
it is not a terminal-only signal. Applying that barrier to every yield would
incorrectly retain valid running apps. Native cleanup uncertainty is still
observed at the established init/entry/fini exit boundaries, or through an
explicit app `retain_invocation` signal. This correction introduces no new I/O,
storage integrity checks, physical cleanup or native detection callback.

A legacy app that loops after a native-only failure without signaling retention
can therefore remain on the ordinary yield path and may still poll its graph.
This change does not claim to solve that separate limitation. The host native-
busy case verifies that temporary exit-unsafety does not become terminal merely
because the app yields.

## Verification

`bash test/run_retained_yield_test.sh` runs seven production-lifecycle cases:
init/main/fini terminal signals, pending graph release, absent raw callback,
ordinary operation and temporary native exit-unsafety. The retained cases check
zero new provider polls, unchanged mapped app/provider custody and allocation,
queued-child suppression, no fini/unload/quiescence beyond the initiating failed
release, and exact bounded requests `1,1,20,50,50,50,50,50,50`. Foreign-owner,
provider start/quiesce/stop/poll reentry, delay recursion, different Runtime,
preparation-time and post-run calls produce no delay. Native barrier call counts
do not grow during ordinary or explicitly retained yields.

The suite and the existing provider-exit suite pass normally and with
ASan/UBSan (`ASAN_OPTIONS=detect_leaks=0` for this executor). The focused Runtime,
demand activation/retention, retained-app, stream-session, bound-storage and
performance suites also pass. CI now includes both forms of the new suite.
Target build receipts are recorded against the final source commit separately.
Host tests and firmware builds do not qualify physical behavior, power draw,
or actual scheduling on a device. No publication or device action is included.
