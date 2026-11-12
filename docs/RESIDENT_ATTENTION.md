# Capture-safe resident attention (optional API1 extension)

`RiscResidentAttentionV1.h` extends the client buffer, not the original client
structure or Runtime API table. Initialize `extended.base.struct_size` with the
extended size and call `resident_shell(&extended.base)` once. Check the tagged
accessor before reading the suffix. Old clients retain their exact prefix size;
old runtimes/hosts do not imply attention support.

The host registers an observer only while no foreground is running. It may use
already-held, separately qualified observation-only alarm/key/RTC APIs. It must
not acquire, release, step, yield, change focus/power, write settings, or call
ordinary Runtime APIs. Diagnostic logging is the sole exception: it returns
false with no output and no violation, allowing status-transition logs inside
qualified observation providers. The Runtime supplies native monotonic milliseconds and
borrows only the host authority pointer; it does not switch native memory,
revoke foreground grants, or invoke the app-exit barrier. The native
`Port.providerStorageSafe` hook is required and checked before/after callbacks;
there is no app-exit fallback. Public Runtime13eca CpuPort.cpp implements this
as a read-only retained/closing/lock/transfer scan permitting healthy I2S/RF.
Absent hooks are INCOMPATIBLE; unsafe native custody is terminal RETAINED. The Runtime API pointer
remains visible for provider identity fences. Forbidden Runtime entry or reentry
latches a violation and retains both invocations without cleanup.

The foreground query returns copied supported/pending bits only on OK. Outputs
are untouched and invalid on every other status, including BUSY. An alarm bit is
an attention request, never an alarm occurrence/token or permission to dismiss.
The foreground must stop capture/background, settle its scene/input, then use
ordinary POLL/POLICY/CONTROLS checkpoints to handle attention. Those existing
checkpoint contracts and barriers are unchanged.

A rejected unsaved-work guard may consume only the last successfully observed
manual CONTROLS/EXIT subset via `reject_manual`. The host callback clears only
local manual latches, without I/O. ALARM, zero, unknown or unobserved bits are
invalid. Successful POLICY and foreground start/end expire the authorization.
BUSY does not consume it; callback errors/uncertainty retain custody.

Qualification: `python test/run_resident_attention.py --output DIR [--sanitize]`
executes real Runtime and unloadable host/foreground modules with deterministic
native storage/exit-gate and host observer endpoints. It proves ABI canaries,
live child grant preservation, absence of barrier/ordinary I/O during query,
restoration, stale tokens, rejection and terminal fences. It is not a device,
physical microphone, or Home observer implementation qualification. Existing
resident shell/policy scripts separately exercise unchanged checkpoint behavior.
