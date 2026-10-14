# Bounded owner-side SD settings maintenance

All selectors remain default-off and X4-only. The application KV1/KV2,
atomic-v1, availability-v1, hardware and storage ABIs are unchanged. This change
reuses the existing copied workers, checked SD attempts, native clients and
Runtime owner checkpoints; it creates no task, callback registration or queue.

## Save and acknowledgment

A successful atomic receipt means the edit is durable in native storage. It
still does **not** mean the corresponding SD checkpoint has been acknowledged.
Releasing the last atomic grant can start one two-namespace maintenance sweep
and return while that sweep is pending. Applications must not infer SD durability
from a receipt, release, navigation or the namespace's AVAILABLE state. Existing
SD index verification, durable export marker and matching native acknowledgment
are unchanged; no SDK-level SD completion notification is added here.

Each ordinary owner checkpoint begins at most one sweep or performs one accepted
poll. The yield/discovery hook and cooperate callback also poll at most one step;
they never start a new maintenance sweep. They do not scheduler-drain that work.
The complete directory scan and its SD operation count are unchanged. This is an
owner responsiveness repair, not a claim to reduce total I/O or establish a
hardware deadline. Timing.lastPolls counts the whole accepted sweep;
lastBlockedMs accumulates time spent inside its owner calls (and any final
shutdown drain), excluding time between calls. Cold graph activation and each underlying callback retain
their existing bounds; a stuck worker never becomes safely canceled by timeout.

## Draft and custody fences

Existing host/foreground atomic grants still prevent admission of a new sweep.
Before beginning one, the lifecycle pauses new atomic snapshots/reads/submits in
both namespaces (1 and 6). New grants may be acquired, but their atomic methods
return BUSY until **both** namespaces finish and every native/SD lease is
collected. This includes the unstarted sibling and the already-acknowledged
sibling; neither can expose a draft that the remainder of the sweep invalidates.
A new edit must read a current revision after BUSY clears. Old revisions still
fail STALE. Legacy read-through maps that temporary exclusion to its existing
BUSY result. AVAILABLE continues to describe native authority only.

The native-only pause is owner-thread state, not a lease, retention flag or
worker command. It never revokes accepted atomic work, never changes the worker
gate, and is removed only after the existing complete-custody check succeeds.

Healthy accepted maintenance uses the same narrow app/UI continuation predicates
as discovery. The exact owned SD pin is preserved; direct Files and USB access
keep their raw custody refusals. Restart, update, storage teardown and quiescence
still require full idleness. Unrelated retention freezes polling and retains all
pins. A true cleanup failure revokes app authority without fabricated release.

Final Runtime shutdown scheduler-drains accepted maintenance to acknowledgment.
Discovery is canceled and collected as before. An explicit cancellation latch may
cancel a save, but cleanup must still collect and native pending edits remain
pending rather than acquiring an invented SD acknowledgment.

## Verification

The focused startup fixture exercises 65,537 pending polls with no scheduler
wait inside an ordinary checkpoint and separately verifies final shutdown drain.
The real Runtime/CpuPort/X4/FatFs maintenance fixture covers both namespaces,
intervening UI yields, a deliberately paused worker, in-flight edit refusals,
sibling fencing, late stale submissions, later successful edits, Files/USB
exclusion and resumption, cancellation and retained custody. The initialized-history fixture creates retained history through real successful
checkpoints after a seeded authoritative generation 1. Draft-fence tests await
collection rather than treating a grant release as an SD completion event.

The legacy maintenance runner pins X4 c7404623 and has no native-capacity
admission. Its first-creation/history mode returns Invalid with both the old and
new lifecycle because the current first-checkpoint implementation requires that
capability. That is recorded as a pre-existing fixture limit, not waived or
counted as a first-creation pass. The separate complete-path fixture uses typed
capacity inputs (X4 3c32f85d, Drivers 11bc45d2); its test awaits the new bounded
completion boundary, but running that full-path qualification is separate.

These are host proofs. No firmware selection, product activation, serial/device
access, flash, merge, release or hardware qualification is performed.

### This checkpoint

The 16 focused maintenance cases pass normally and with ASan+UBSan. The actual
startup/coordinator fixtures also pass normally and sanitized; Runtime owner,
resident-host/foreground/return draft fences and availability contracts pass.
The preserved receipts and source/binary hashes are in
`evidence/bounded-settings-maintenance-20261012/summary.json` and its adjacent
compressed manifests and logs.

At 100 retained generations per namespace, paired final-grant release performs
zero polls and zero scheduler waits, followed by 1,082 bounded owner polls and
876 queued SD operations. A single dirty namespace performs 541 polls and 438
queued operations. Each yield advances at most one poll in this fixture. Modeled
owner blocked time is zero; total synthetic elapsed time remains 1,600 ms paired
and 800 ms single. The model is seeded with an already-authoritative generation
1; its media allocation differs from the historical first-creation fixture, so
the elapsed totals are not a hardware measurement or an exact before/after wall
time comparison. Normal and sanitizer deterministic counters agree.

Single-job Xtensa compile-only checks pass in legacy and cohort layouts, with
empty default-off lifecycle/maintenance/bootstrap objects. The coordinator stays
320 bytes. Static target frames are 64 bytes for checkpoint, 48 for maintenance
poll and 32 for cooperate/pause. A complete selected firmware relink and physical
qualification remain unrun for this change.
