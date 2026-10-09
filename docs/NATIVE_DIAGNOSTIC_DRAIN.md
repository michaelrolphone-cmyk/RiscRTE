# Optional native diagnostic drain (Runtime 0.1.76)

`RISC_NATIVE_DIAGNOSTIC_OBSERVER=1` compositions can additionally implement
`extern "C" void risc_native_diagnostic_drain(void)`. The optional weak hook
runs on the diagnostic owner task after the borrowed-line observer has returned
and after the output guard is released. It runs even when live output returns
early because USB is absent, its FIFO is full, or diagnostic replay is active.
Null lines, foreign tasks and reentrant output do not call either native hook.

The observer remains bounded and free of allocation and I/O. A platform can
copy selected milestones there, then use the drain for bounded storage work.
The drain must not call Runtime, providers or diagnostics, and must guard its
own reentry. Storage failure must not alter boot policy or erase settings.
Runtime itself adds no filesystem, NVS, task, platform pin, provider or app ABI.
Compositions without a drain implementation retain existing behavior.

This change is based on Runtime 0.1.75 (317b74e363877cb6e5a90198211f897744e4a6d3),
preserving the HCI burst, Wi-Fi diagnostics and stream-retention fixes used by
X4 0.1.29. The X4 product supplies the persistent journal independently.

Run `python3 test/native_diagnostic_drain.py` for the source-linked RAII fixture.
Target firmware compilation and hardware behavior are separate checks.
