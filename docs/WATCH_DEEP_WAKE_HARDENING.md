# Watch deep-wake hardening

Source-only successor of delivered Runtime `3b90cd11cf0ff4e8116bb54d2d9eb9c7c67a148b`.
The Watch 1.1.10 product, app/store payload, and Files corrections are unchanged.
No physical wake claim, flashing, or full-image replacement follows from these tests.

## Reproduced defects

1. The native deep callback attempted a bounded crown-supervisor pause after
   the display had already been blanked and its backlight held. A mutex timeout
   returned from a void terminal callback. CpuPort correctly treated the return
   as uncertain hardware custody and retained the application, preventing the
   caller from restoring the dark display. This was a clean refusal incorrectly
   presented as a terminal-entry failure.
2. A fresh supervisor after external deep wake did not initialize the waking
   gesture boundary used by light wake. An actual crown down/late release was
   delivered as a new short action. Clock maps that action to manual sleep after
   its startup guard, and native host navigation could also receive it. A held
   wake released after startup can therefore immediately resleep.
3. The supervisor can acknowledge a key IRQ between application preparation
   and native entry. Checking the physical line alone cannot see that pending
   software history. The new quiet preflight refuses pending/held key history
   before the sampler is paused; the subsequent native GPIO checks cover events
   arriving after pause.

These are confirmed source-level defects. The user's black-screen episode is
not yet attributed to one particular path without device evidence.

## Repair boundaries

The native-only Hardware suffix adds paired deepPrepare/deepCancel hooks.
The public hardware/provider ABI is unchanged. Clean prepare refusal occurs
before terminal entry and returns BUSY after cleanup, allowing the existing
app/panel restore path. Failed cancellation or an unexpected terminal return
still retains custody. Single, timed, and multi-source deep entry share this
contract. The crown pause is removed from the void terminal callback.

On the first successfully initialized supervisor after external deep wake,
only the key history present at the wake boundary is interpreted as a waking
gesture. Motion-only wake with no pending key history does not swallow the
next press. Timer/cold boots and provider reopen do not manufacture a new wake
boundary. Long-hold recovery continues to observe the real key edges.

GPIO21 active-low selection, RTC_PERIPH retention of the claimed pull-up,
RTC-to-digital routing on a fresh claim, the backlight safe-state-before-unhold
sequence, PMIC rails/charging, and the selected native/app features are unchanged.

## Verification

`bash test/run_crown_recovery_test.sh` covers delayed/coalesced external wake,
EXT0/EXT1/GPIO, motion-only and timer/cold wake, failed first bus read, provider
reopen, a pending software event after hardware ACK, held-key admission, mutex
refusal, long hold, app exit, and recovery watchdog behavior. Run again with
`SANITIZE=1 SANITIZERS=undefined` for the production supervisor.

The same delayed-release fixture linked against exact original production
NativeCrownRecovery.cpp fails its no-new-short assertion (exit 134). Only its
pause call signature is adapted to the old header; the executed wake scenario
uses the unchanged original production implementation.

`bash test/run_deep_prepare_test.sh` and `SANITIZE=1` cover all four deep forms,
quiet/read/arm/timer/cleanup failures, held-backlight restoration after clean
refusal, callback-pair validity and terminal-return retention. The full existing
`bash test/run_deep_sleep_test.sh` remains required, along with runtime host
control and the exact selected crown native target build.

Host transport, SDK calls, PMIC state, and screen visibility are modeled.
Physical repeated deep wake, held wake, timed-to-deep transition, and battery/USB
runs are still required before hardware qualification. This document does not
claim those checks passed.
