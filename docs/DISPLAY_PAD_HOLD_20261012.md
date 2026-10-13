# Display GPIO pad-hold startup and resume repair

Observed X4 source: `9a510b049483ed20327be2c972b12359d2877323`.
The supplied device log reaches `default.elf`, rejects frontlight startup and
quiescence, and retains the failed provider before the panel is activated.

## Reproduced defect

`Port::gpioDeepSleepHold` treated latching one owned static output like entering
whole-device sleep. Unrelated pending app-data maintenance, HTTP, USB, IQ or
native storage activity returned BUSY. The real X4 frontlight must hold its two
LOW outputs during startup and shutdown; the same refusal defeats both paths.
The Watch panel uses this callback to release its backlight hold during resume.
A normal pending background write must not strand that display asleep.

The repair changes only per-pad admission. It preserves the owner task, exact
scope/token and output checks, the PWM exclusion, native rollback and poisoning,
retained-state fencing, reentrancy exclusion, and the live storage worker's own
GPIO scope. Actual device sleep, app-exit and restart predicates are unchanged.
No provider ABI, waveform, application payload, setting, partition or feature
selection changes. This is not a rollback of the settings/file-source work.

## Verification and limits

The real X4 driver and CpuPort reproduce start=false/quiesce=false before the
repair and true/true after it under injected unrelated activity. Original
frontlight tone and refused-retirement cases also pass normally and with
ASan/UBSan. The permanent suite additionally selects native storage custody and
checks owned-pad safety controls. A second test puts the real CpuPort callback
beneath the production Watch panel's existing sleep/resume harness.

Run `bash test/run_display_startup_test.sh` and
`python3 test/display_startup_watch_test.py` with RISCRTE_RUNTIME_ROOT,
RISCRTE_READER_ROOT, RISCRTE_X4_ROOT and RISCRTE_WATCH_ROOT pointing to the
checkouts pinned in `.github/workflows/display-startup-regression.yml`.
Use SANITIZE=1 for ASan/UBSan. PAD_HOLD_EXPECT_BROKEN=1 selects the negative
control against the exact old Runtime; it is not a production feature flag.

These are software reproductions with modeled peripheral I/O, not physical
recovery evidence. The supplied X4 log lacks the specific GPIO denial code, so
it does not prove which background condition was present on that unit. No Watch
failure log has been matched to an exact flashed image. The cross-layer tests
establish a concrete shared defect, not proof that every reported display issue
has this cause. Native target CI compiles qualification profiles, not cumulative
product recovery images. Do not flash their component binaries as full firmware.
