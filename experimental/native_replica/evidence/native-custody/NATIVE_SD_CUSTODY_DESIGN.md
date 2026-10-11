# Native SD attempt custody and cold bootstrap: design for review

Reference Runtime: published `e471163431b0ad2d324c81db4b55c13957c93699`, tree
`bfd5f7854a45a7db8e655ffd5c09d600f4ead2b3` (catalog, provenance and terminal checks).

Status: proposal only. No implementation, SDK ABI, provider, production or product
activation is authorized by this document. The existing strict consumers remain
unchanged. This is explicit background persistence work, never a launch/readiness
scan. Internal admission implements existing user-authorized persistence policy;
safe ordinary backup/restore should become automatic once qualified.

## Recommendation

Add one native-owned **attempt coordinator**, with a private Port admission fence
and a typed child-session interface. Reuse the existing copied async worker and
existing provider-scoped SyncV1 admission semantics. Do not wrap the public
observer to manufacture READY. Keep the selected provider, dependencies, native
transport scope, buffers and core-operation contexts pinned through the attempt.

Two distinct fences are required:

1. The exact async session protects the worker and provider while open.
2. An outer native attempt protects the interval before Begin, between child
   sessions, and between checked End and candidate transfer/native completion.

The smallest warm-capable provider change is an atomic begin-time refusal of all
preexisting file/directory handles, after rejecting an existing session and while
ordinary selected-device admission is already fenced. This changes no table
layout. Until that provider behavior is implemented and qualified, admit only a
fresh, exclusively controlled activation whose zero-handle state is established
by construction; refuse arbitrary warm instances. Merely asserting a fixture
`held=true`, or checking READY, does not establish either admission mode.

## 1. Current source facts

Reference prefixes: R is Runtime, X is X4, D is Drivers. Exact checkout revisions,
file hashes and published equivalents are in SOURCE_MANIFEST.json.

- X deliberately starts unmounted; the first worker REFRESH owns card delays and
  mount I/O. [X minimal/drivers/x4pro_sd/driver.c:634–642]
- Its observer returns UNAVAILABLE while any async session exists, before reading
  worker-mutated fields. The public state contract says READY is a snapshot,
  UNAVAILABLE combines absent/exported/sleeping states, and RETAINED forbids further
  volume calls or grant release. [X driver.c:552–570; R sdk/driver/RiscStorageVolumeStateV1.h:2–11]
- Current checked consumers require READY before Begin/Refresh and while polling.
  They therefore cannot bootstrap or run against that actual active-session
  profile. The newly tested terminal fix adds post-End RETAINED precedence; it
  does not solve bootstrap or in-session observation.
- X's live session rejects owner synchronous filesystem/export/sleep/quiesce
  calls, even when the mailbox is idle. Its begin omits `has_handles()`. REFRESH
  short-circuits when ready() is true, so it cannot prove handle absence.
  [X driver.c:103–131,539–550,669–685; R sdk/driver/RiscStorageAsyncAdapterV1.h:103–105]
- SyncV1 already permits is_owner=false while admission is fenced. Native Port
  stores separate sync contexts by device instance. [R sdk/driver/RiscProviderSyncV1.h:16–27;
  src/ports/esp32s3/CpuPort.h:130–134; CpuPort.cpp:232–237,613–617]
- Native mailbox busy only means non-idle. USB PHY claim explicitly is not a
  storage fence. Native light/deep sleep and wake/hold paths can have an idle gap.
  [R CpuPort.h:138; CpuPort.cpp:310–318,715–725,770–779,827–832,850–861,909–918]
- Worker faults can exceed what the provider snapshot reports: native power failure
  latches StorageWork::fault, while X's async init returns immediately on failed
  io->power. A final native scope check is therefore required alongside provider
  observation. [R CpuStorageWork.inc:69–92; X driver.c:328–342]

## 2. Ownership and authority

### Native owner

The Runtime-owned controller, not an invocation/app or removable-card record,
selects the exact configured provider/device instance and protected namespace.
Its native graph grant pins mappings/dependencies independently of application
buffers. Reserve grant and memory capacity before starting. Existing loader and
board validation remain the sole provider-activation authority.

An explicit background request may acquire/activate the configured provider
through the existing trusted native path. Controller construction and readiness
queries must not load a provider, mount, enumerate, or scan settings. Activation
is a separate checked phase under existing graph authority, completed on the
serialized owner before establishing the attempt fence and exposing competing
grants. The async one-use permit below must never be widened to run arbitrary
graph activation callbacks. No speculative load, broadened grants, or
first-app-launch scan is introduced.

The attempt is volume-exclusive for the first implementation. This is simpler
than claiming independent namespace concurrency on the single shared filesystem.
It freezes ordinary selected-provider admission across every child session, but
never closes someone else's handle. Prior handles cause deferred admission.
Native settings reads stay on the core. New native edits remain admissible through
existing serialized native jobs; the SD attempt does not lock the native map for
its whole duration. Rebase/export ticket ownership remains mutually exclusive,
and current durable-marker/frontier checks retain newer edits.

### What an attempt does not authorize

It provides execution/custody evidence, not source choice, initialization,
creation, adoption, migration, deletion, or semantic reconciliation authority.
Existing role-specific path and mutation checks still apply. Catalog Report and
reader Candidate remain owned historical content. A later native mutation must
separately possess current baseline/marker/rebase authority and a live attempt.
Wi-Fi interpretation remains in the app-owned reconciliation worker.

### Private identity

Use a native token bound to:

- The exact native owner/controller and selected graph grant generation.
- Device/scope instance, provider profile and native-configured namespace.
- A nonreused attempt serial and current transport mount/custody serial.
- A cancellation/revocation latch and sticky retained state.

This token never comes from SD, application arguments, a generation number, or a
profile fingerprint. It is not persisted, logged as card identity, or carried
through reboot. Counter exhaustion refuses admission instead of wrapping.

## 3. Minimal private API and state choices

Names below describe an internal C++ interface, not a public SDK layout.

- `acquire(binding, purpose)` reserves the exact provider pin and provisionally
  fences ordinary admission. It commits attempt admission only after acquiring
  its own new session through checked Begin. Returns Admitted, Busy/Unavailable,
  UnsupportedProfile, or Retained.
- `bootstrapStep()` completes the initial admission session: one worker REFRESH
  if mounting is needed, or a checked empty close for qualified warm admission.
  Both routes perform final owner observations before ReadyClosed.
- `prepareNative(role, job)` runs native snapshot/marker/budget admission on the
  native settings worker, then transfers its owned immutable preparation through
  checked owner collect. It issues no SD calls or owner dispatch permit.
- `openChild(role)` grants one exact child session at a time. The role restricts
  directory/package operations and fixed protected paths; no app-selected path.
- `submit / collect / end` own exact copied tokens and owner result buffers.
- `pollCustody()` returns a typed state: ReadyClosed, OwnedSession, Pending,
  ClosedUnavailable, RevokedDraining, or Retained. OwnedSession is not READY.
- `requestCancel(reason)` revokes publication permission immediately if it wins
  before the native worker claims Committing, then drains only the exact owned
  session/job. After that claim, cancellation defers until checked native
  completion and reconciles the actual root result. It never cancels another actor.
- `issueCompletionReceipt(action, job)` runs only on the SD owner after checked
  closure and creates a private copied receipt plus custody pin.
- `commitNative(receipt, pin)` runs only on the actual native settings worker under
  core serialization. It invokes no SD API, including public observe.
- `collectNativeCompletion(job)` runs on the owner, fences the worker result and
  reconciles pin/cancellation/retention before finishing the attempt.
- `finishAttempt()` is permitted only after closed custody and transfer/native
  completion or clean cancellation. Checked graph release can itself retain.

Lifecycle: Idle → Acquiring → AdmissionSession (optional worker REFRESH) → ReadyClosed →
ChildSession/Pending → Closing → ClosedHeld → NativeCompletion (optional) →
Released. Multiple sequential children are allowed within the same outer fence.
Any phase can enter RevokedDraining; uncertain custody enters sticky Retained.
A timeout, cancellation request, idle mailbox, or clean child close is not release.

The controller owns the async session serial, operation token, result buffer and
end state. Clients cannot substitute session/operation tokens. A malformed/stale
operation result refuses progression; uncertain ownership retains dependencies.
Bound all queues and poll work. PENDING/BUSY performs no extra volume I/O.

`Guard::held` must use this real attempt state, not provider BSS. Worker-side guard
checks use immutable bound fields plus native internal-RAM atomic revocation
flags; they must not race owner writes or read unfenced transport fields. Existing
native worker/serialization admission supplies Guard::worker. Do not introduce
64-bit atomics in provider PSRAM merely to store serials: transport fields can be
sampled after the actual completion fence; owner-to-worker revocation can use the
already-supported internal-RAM atomic width.

## 4. Implementing the outer fence without a new SDK table

### Selected-device SyncV1 admission

Use the documented admission-fenced is_owner behavior in the selected native
Port::Sync context. While an attempt is held, ordinary selected-device owner
admission returns false. The coordinator opens a non-reentrant, native-only call
permit around one exact begin, async-submit, package-submit, collect, or end invocation. These current
owner methods copy metadata or interact with the mailbox; they do not execute
volume I/O. The permit is not held across a yield or a user/provider service
callback. It is scoped to the exact attempt and device, never a global owner flag.

Each reviewed X4 async wrapper calls async_owner/is_owner exactly once at entry.
Implement the permit as a **one-use Sync admission credit**, not a boolean which
remains true throughout the call. There are five dispatch kinds: begin, async-submit, package-submit, collect and
end. The selected wrapper consumes that credit;
subsequent same-device is_owner calls see the fence again, including reentrant
ordinary volume calls after end cleared its session. Raw native mailbox methods
receive separate exact-method/token permission; they do not renew Sync credit.
One end call may cancel the exact old operation or collect the exact cleanup
operation, then submit a zero-operation checked-cleanup request. Its raw permit
must allow this bounded sequence and record the minted cleanup token, rather
than allowing only one call or authorizing arbitrary submission. Repeated
PENDING/BUSY polls each receive a fresh one-use owner dispatch credit, never a
carried-over credit.
Require the scope already claimed/bound; this permit does not authorize provider
start, async_claim_scope or sleep-resume setup. Require owner-task **and non-ISR**
admission: current src/main.cpp:87 checks a task handle only, whereas
NativeStorageWorker::isWorker already excludes ISR context. The private gate must
not inherit the task-handle-only predicate as sufficient proof.
Reject nested controller calls. Clear the call slot unconditionally on every
return, including PENDING, BUSY, cancellation and errors; no permit is stored in
the copied request or pending job.

The proof depends on a verified straight-line dispatch: no callback, allocation,
logging, graph service, yield or scheduler call between arming the credit and
directly invoking the exact selected wrapper. Its inspected entry consumes the
credit before it can call any native mailbox function. Unknown/changed profiles
are refused. If a provider needs a second is_owner check, fail closed and review
that profile; do not silently replenish credit. This is a trusted-code authority
boundary, not a memory-isolation claim against arbitrary native corruption.

The existing worker path stays separately scoped. Do not fabricate worker task
identity or give the worker ordinary Runtime/app privileges. Public observe stays
side-effect free and truthful; its UNAVAILABLE return is not translated to READY.
No application/provider import can create the private permit.

This seam also fences the actual X4 ordinary filesystem, export, provider sleep,
service and quiesce paths between child sessions, since they consult valid_task,
async_owner or the same owner guard. It is a reviewed profile for this provider,
not an assumed property of every storage.volume implementation.

Before setting the provisional fence, prove no held sync lock, active callback or
unfinished mailbox operation. An idle mailbox does not prove session absence,
and the public API cannot identify another actor's idle session. Under the
provisional fence, dispatch exactly one permitted Begin. The provider rejects any
existing session before inspecting worker-mutated state. Commit attempt admission
only after Begin returns a newly issued owned session. On a clean refusal, restore
the provisional fence without adopting, cancelling or touching the other session.
Any uncertain ownership retains the pin. Native accounting from a proven fresh,
exclusive activation may establish session absence by construction; do not infer
it from READY or mailbox idleness. Sync unlock/destroy must remain available as
their ABI promises; do not change cleanup semantics to manufacture exclusion.
Direct calls that bypass Sync admission need the additional native gates below.

### Begin-time prior-handle exclusion

Future X4 async_begin should reject an already active session before inspecting
worker-mutated provider state, then under fenced owner admission reject every live
file/directory slot and incompatible lifecycle state before delegating to the
existing adapter begin. No handle is forcibly closed. A generic BUSY result does
not disclose which specific precondition failed. Reject before any REFRESH,
including its ready()==true shortcut.

This warm-admission guarantee must be bound to the actually validated provider
profile/version and source identity. Current async_version=3 alone does not prove
it. Until qualified, fresh-exclusive activation is the only safe constructional
zero-handle admission; an arbitrary warm grant is Unsupported/Deferred.

### Separate native bypass gates

The same outer state must gate selected native worker bind/submit/unbind/release
and the owner branch of workIoAllowed; existing scope/pin ownership is reused,
never claimed a second time. Read native scope faults only at an idle/completion
fence. Audit raw GPIO/SDMMC callbacks and same-device dependency paths, rather
than assuming they all consult provider Sync.

Gate USB PHY/export initiation, light/deep/timed/set sleep, wake/hold/rail changes,
provider graph teardown and relevant reset/unload entry points across idle gaps.
A healthy active attempt means Defer/Busy; retained custody means Retained. Do not
feed healthy attempt activity into a generic unsafe-storage predicate that turns
normal work into permanent invocation retention.

For app navigation, first request cooperative drain where current exit barriers
require an idle mailbox. Call the existing release/exit barrier only after that
preparation succeeds. Native-only objects may outlive an app only when their
separate provider pins and contexts are already established. This design does
not authorize unloading borrowed app buffers or bypassing the existing barrier.
Graph release must not revoke the last protective grant and then discover that
quiesce was fenced. GraphV2::release marks the grant pending-release before
quiescence; ModuleV2::reportQuiescence can then call X4
bootlog_descriptor_error, which reads error buffers without the Sync guard.
[R src/runtime/drivers/ProviderGraphV2.cpp:433–438,464–489;
ProviderModuleV2.cpp:396–430; X BootLog.h:192–203] Refuse/defer this transition
before grant revocation and before any descriptor diagnostic callback, including
while the worker can mutate those buffers. Prepare lifecycle before graph release; retain the pin until
its checked release succeeds.


### Explicit owner/worker completion split

All SD begin/submit/collect/end/observe calls run on the actual SD owner task.
`Guard::worker` and a host boolean do not change that requirement. In particular,
the current writer acknowledgeNative and recovery completeNative call public
observe despite requiring worker admission; those raw-volume forms are not a
qualified live native-worker integration. The proposed native-profile overloads
must replace that call with an owner-issued receipt. No READY is manufactured.

Preparation also needs an explicit handoff: capture the complete export snapshot,
prepare its durable marker when requested, copy recovery metadata, and sample the
actual native image/header budget under native serialization. Fence that job's
result before the owner starts SD phases. The owner receives immutable owned
preparation, not permission to read a concurrently edited core. Each mutable
consumer/job context has one task owner at a time; ownership changes only through
the copied-job completion fence. No owner poll overlaps worker preparation or
native completion on the same consumer. This does not freeze native edits for the
whole SD attempt; final native admission still rechecks current core invariants.

The owner may issue a receipt only after final provider observation, a fenced
native-scope snapshot, checked child close, exact content verification, and a
still-held outer lease. Bind the receipt to attempt/provider/mount serials, closed
child sequence, namespace, operation kind, native job identity, and the exact
verified SD identity/count/size or legacy source fingerprint as appropriate.
Construction/validation is private; copying arbitrary bytes does not issue a
receipt. It carries custody evidence only; existing core marker/ticket/baseline
authority independently admits the requested native transition. Wipe receipt
content and never log profile-derived fingerprints.

The actual native settings worker consumes the copied receipt through an opaque
custody pin, under the existing core serialization lease. It does not call the
provider, graph, owner-only observer or arbitrary application callback. Immutable
receipt fields are published with the native job's release/acquire fence. Shared
revocation/claim state is native internal-RAM atomic state. A plain unsynchronized
held=true callback is insufficient.

The existing NativeReplicaJob is a disconnected copied-job facade, not a live
settings-worker binding. The preferred future binding is a fixed native-only job
slot polled by the existing native storage task, without rebinding or borrowing
the selected provider's mailbox. Its fixed trusted callback owns the native core
serialization lease. Dispatch must carry a native-only execution kind:
ProviderIo with the exact provider/scope, or NativeSettings with the exact job/pin.
Current workIoAllowed admits a matching scope from any isWorker call, so the
future gate must also check that execution kind. Apply the same ProviderIo-kind
and exact-scope condition to storageWork_.io.on_worker: X4 valid_task/guard_enter
use it to bypass owner/Sync admission. Gating raw I/O alone is insufficient.
NativeSettings has neither raw SD nor provider-worker admission even on the same
physical task. Include the native slot and its retained/completion pin in all
idle/exit/sleep accounting; NativeStorageWorker::idle currently sees only the
provider mailbox. No callback pointer comes from an app or card. Scheduling and
memory admission of this slot remain implementation gates.
[R experimental/native_replica/NativeReplicaJob.h:5–8,24–35;
src/ports/esp32s3/NativeStorageWorker.h:17–24; CpuStorageWork.inc:3–7,67;
X minimal/drivers/x4pro_sd/driver.c:104–105,113–125]

Define the cancellation linearization explicitly: before the worker claims a
Queued receipt, owner cancellation/revocation can win and cause zero native
mutation. Once the worker claims Committing, the outer pin is non-releasable until
its checked native completion; USB/sleep/unload and cancellation requests defer.
Do not invalidate/recycle the epoch underneath that commit, or promise to undo a
root publication that already won the race. Existing checked native transitions
may return complete old/new roots or retained/uncertain results; preserve that
result and all associated custody. Unexpected loss during commit is a retained
failure case, never justification to drop the pin or assert no native mutation.
No SD operation is in flight or newly submitted during native completion.
Once the native job is issued, cancel through the receipt's atomic revocation/
claim state and always collect its result. Do not invoke NativeReplicaJob::cancel
for this path: the underlying mailbox can discard a running/completed result,
which would hide the actual native root outcome when commit won the race.
An unclaimed revoked job runs only its bounded no-native-I/O refusal and returns
that result through the normal completion fence.

After the worker result is fenced, the owner rechecks the pinned attempt and
provider/native custody before releasing anything or exposing completion. A
native Retained/Uncertain result or post-completion retained custody preserves the
required contexts and dependencies. New native edits still use existing revision
and captured-frontier checks; the receipt cannot authorize clearing newer groups.

## 5. Cold bootstrap and exact-session observations

1. Reserve the provider pin, establish the provisional outer fence, and obtain a
   new owned session through Begin's existing-session and qualified prior-handle
   checks. Only then commit attempt admission. A clean UNAVAILABLE snapshot may
   admit this bootstrap sequence under the qualified native profile; it says
   neither absent nor vacant. RETAINED refuses all new I/O. A clean Begin refusal
   restores the provisional fence and leaves all prior sessions/handles untouched.
2. Mark prior attempt/epoch receipts invalid before any mount/refresh attempt.
   If mounting is needed, submit exactly one REFRESH in that admission session
   through the real copied worker. No owner mount I/O. Qualified warm admission
   uses a checked empty close instead of REFRESH.
3. During this owned session, use exact copied collect/end results and the private
   native fence. Do not read worker-owned BSS or require public READY during work.
4. Close the bootstrap session through real end. Only after end OK, observe
   provider custody and sample the fenced native transport scope. RETAINED/fault
   outranks IO, cancellation and simultaneous lease loss.
5. Establish ReadyClosed only with successful collected REFRESH (or the qualified
   warm-admission proof), end OK, READY, a healthy native scope, unchanged selected
   binding and live outer ownership.
   Clean unavailable mount failure yields no candidate/vacancy and may retry only
   as a separate later attempt. Uncertain cleanup remains retained.
6. Capture the new mount/custody serial after that successful boundary. Warm
   admission may skip REFRESH only under the qualified handle/epoch profile.
7. Once evidence gathering/publication starts, forbid REFRESH, power cycling,
   remount and automatic transport reinitialization within the attempt. Existing
   consumer Refresh phases must be explicitly skipped/replaced in the future
   native-session overload; silently routing them to a no-op READY wrapper is
   not acceptable.
8. For every child, end OK is followed by provider and native-scope observations
   before any successful, failed or cancelled safe exit or output. Hold the outer
   fence through later take/acknowledge/completeNative and their native job fence.

All four direct consumers and the catalog need one reviewed typed session path;
their existing raw-volume strict mode should remain the default until integration
is qualified. No consumer may independently redefine in-session UNAVAILABLE.

Any copied transport IO result revokes further normal operations for that attempt,
then drains the exact session. Do not let a caller reinterpret ambiguous IO as
permission to remove an orphan or continue writing: the current writer treats an
ExistingPage IO as a possible orphan path, so this continuation needs explicit
review in the native-profile integration. Content-invalid and transport-uncertain
outcomes must remain distinguishable where the provider actually distinguishes
them. Cancellation can discard a completed RECOVER result; it never proves that
recovery did nothing.

## 6. Mount/custody epoch and physical limits

Track a monotonic native transport serial outside any reset-to-zero scope struct.
Invalidate/advance it for claim/release, open/close, power/remount attempts,
export/sleep transitions, observed transport fault, binding replacement and reset.
Keep a publication-revoked latch even if later successful cleanup clears a native
fault. Take coherent snapshots only after worker completion; never poll a live
64-bit worker field from the owner. During bootstrap, transitions are expected;
after ReadyClosed they revoke the attempt.

The pair (provider grant generation, transport serial), plus the nonreused attempt
serial, prevents software ABA through an old pointer or recycled session number.
A clean planned lifecycle change invalidates old evidence. A fault/unknown
cleanup retains custody. Both require a new attempt and complete verification
before any later publication/adoption decision; neither imports SD authority.

This is **single-attempt software continuity**, not durable physical-card identity.
NativeSdmmc configures NO_CD and exposes geometry/clock rather than CID. A CID
alone would not prove uninterrupted custody or prevent removal/reinsertion of the
same card. No new credential-like persistent card identity is proposed.

A qualified no-remount transport that fails closed on every observed transfer
failure may suffice for the explicitly stated single-attempt failure model.
Successful cached metadata/EOF does not prove physical presence at every moment.
The pinned IDF transport must be reviewed for implicit reinitialization/retries;
card removal/swap/cache interleavings require physical qualification. If that
model cannot support the needed publication guarantee, fail closed and review
additional change-detection/transport evidence. Do not label a host epoch test as
physical card-swap proof or assume missing CID is itself the blocker.

## 7. Implementation increments after approval

1. Disconnected private attempt/session state machine and native permit model,
   using the actual Worker/X4/FatFs composition and fresh-exclusive admission.
   No production binding; strict public observer retained.
2. Reviewed provider begin-time handle exclusion and private Port lifecycle/scope
   gates. Keep existing SDK layouts; observe provider manifest/version rules for
   any eventual distributable provider edit.
3. Explicit typed native-session integration across readers, writer, recovery and
   catalog, with the terminal checks and IO-continuation review. Test held outer
   custody through actual native durable acknowledgement/rebase completion.
4. Total native/core/provider/worker/heap/stack admission and target compilation,
   then separately authorized hardware qualification and product activation.

A passing isolated state-machine test does not complete increments 2–4.
