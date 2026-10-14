# Native SD attempt and typed consumers

This is experimental native composition, with an explicitly selected
[Runtime/Port graph bridge](NATIVE_RUNTIME_CUSTODY.md). Product startup remains
unselected. Existing SDK prefixes are unchanged; a separately versioned additive
SAD1 suffix enables checked warm admission only for providers that attest it.
Fresh remains the default; arbitrary warm admission remains unsupported.

## Attempt custody

`NativeSdAttempt` gives one-use, exact-device Sync credits to the five reviewed
X4 async wrappers, checks exact raw scope/request/token dispatch, and permits the
bounded checked-End cleanup sequence. Admission performs one real copied REFRESH.
The public observer remains truthful: an owned session is UNAVAILABLE, not READY.
Every checked close observes actual provider and fenced native-scope custody;
Retained outranks IO, cancellation and lost lease.

`NativeSdSession` is a separate typed interface, not a manufactured volume table.
It owns the child tokens through the attempt, restricts operations to the native
namespace and consumer role, and pins closed custody until output/native
completion. One root consumer is allowed at a time. Catalog and recovery may
lend one exact child slot while retaining their own pin. Public attempt methods
cannot steal a typed child's session. An IO return from submit or copied collect
revokes all normal continuation, including the writer's old IO-to-orphan path.
A clean closed failure may drop a consumer pin; retained custody never may.

## Preparation and completion

All five consumers have explicit `prepareNative`, `preparationWorkerStep` and
`collectPreparation` entry points. Native preparation captures snapshot/marker or
actual core framing/capacity under the supplied worker+core-serialization guard.
The owner cannot step the mutable consumer until the copied job is collected.
Recovery quiescence callbacks run on the owner, with a custody recheck after
callbacks; the native preparation worker does not invoke them.
Catalog children use that captured framing without reading a mutable core on the
owner. Child REFRESH phases are explicitly skipped; no observer wrapper converts
UNAVAILABLE into READY. Existing raw-volume entry points retain strict behavior.

Reader Candidate and catalog Report are historical content only. `take` performs
final owner custody checks and drops only its own pin. It confers no initialization,
adoption, migration or source-choice authority.

Writer and recovery expose `issueNativeCompletion`, `nativeCompletionWorkerStep`
and `collectNativeCompletion`. The owner issues exact verified identity/count/
size/frontier evidence only with a checked closed child and held attempt. The
native worker consumes it with the core's existing ticket/marker authority and
never calls SD or public observe. Newer native edits retain existing revision and
captured-frontier protections. Exact known generation-zero bases are valid for
unpublished retirement.

Completion mailboxes are permanently constructed members. The owner binds the
attempt before mailbox publication; a persistent worker can poll the stable job
before issue and after collection. There is no owner-mutated placement-object
pointer or per-job destruction. Terminal native Busy requeues a checked receipt;
it cannot strand a caller in a pending-style Busy with no job. The scheduler must
still stop/fence access to the entire consumer before its destruction, as for any
worker-owned context. Never destroy a pending or retained consumer/attempt.

Cancellation is atomic and the issued native job is always collected. Before
commit claim, cancellation produces no native mutation. After claim it preserves
the actual core result, including an already-published root. A failed or retained
final custody boundary cannot return Done/Ok. Never use NativeReplicaJob mailbox
cancellation for this completion path.

## Evidence and remaining gates

The source-pinned host composition uses unchanged X4 observer/driver, Runtime
StorageWorker, Drivers FatFs and real sector-backed file operations. Synthetic
card content is built in separate seed processes; tested cases start fresh
provider processes. The tests cover all five consumers, preparation separation,
sequential catalog children, strict raw refusal, outer pins, transport failure,
malformed results, newer edits, cancellation races and terminal retention.
See `test/NATIVE_SD_CONSUMERS.md` for the runner and coverage.

`test/compile_native_sd_consumers_target.sh` compiles all typed APIs with the
pinned ESP32-S3 Xtensa compiler and emits fixed object sizes. These are compile-only
layout evidence. Native worker/control objects contain atomics and must reside in
native internal RAM; target heap/stack/total admission is not established here.

The selected bridge separately qualifies graph pins, native scope epochs, exact
task/non-ISR admission and lifecycle/raw gates against actual Runtime and CpuPort.
Native settings-worker scheduling/core serialization, the real production
backend and total memory admission remain separate product qualification work.
Host task predicates and deterministic backend fixtures do not prove physical
hardware or those remaining product properties. No physical card-swap, removal, durability or electrical behavior is
qualified, and no product enables this path.

Run the source-pinned suites with fresh output directories:

    PYTHONDONTWRITEBYTECODE=1 python3 test/run_native_sd_attempt_test.py \
      --x4 X4_ROOT --drivers DRIVERS_ROOT --reference-reader READER_ROOT \
      --output NEW_OUTPUT_DIR [--sanitize undefined|address,undefined]

    PYTHONDONTWRITEBYTECODE=1 python3 test/run_native_sd_consumers_test.py \
      --x4 X4_ROOT --drivers DRIVERS_ROOT --reference-reader READER_ROOT \
      --output NEW_OUTPUT_DIR [--sanitize undefined|address,undefined]
