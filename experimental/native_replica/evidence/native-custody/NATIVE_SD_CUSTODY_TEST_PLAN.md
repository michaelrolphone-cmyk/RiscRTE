# Native SD custody: composed proof plan

Design reference: Runtime e471163431b0ad2d324c81db4b55c13957c93699,
X4 c8004c5c8362b142ebb35a0e1c5d266532a11581,
Drivers 281609a0d2ae514f107d2ef67b6e8b4d25c72fb8.

This is a test specification and proof argument, not a report that the proposed
adapter exists or has passed these tests. No implementation or activation is
included. Existing source observations and separately executed baseline suites
are distinguished below.

## 1. Composition and assertions

The next host fixture should join, without substituting its semantics:

- Actual checked native store/core and native copied job.
- Proposed private attempt owner and Port gates.
- Actual copied StorageWorker and shared async/package adapter.
- Actual X4 async driver, shared Drivers FatFs, and a faultable sector model.
- Actual replica/legacy readers, checkpoint writer, recovery and catalog.
- Graph grant/revocation/diagnostic paths and actual native lifecycle gates.

Use actual owner/native-worker task identities in the harness. A true/false
Guard callback does not prove the owner/worker split. Record which task executes
every callback and assert that SD begin/submit/package-submit/collect/end/observe
run on the SD owner, raw card I/O runs only on its admitted worker, and NVS/AppData
root transitions run only on the native settings worker under core serialization.
Public observe from that native worker must fail the test before inspecting any
provider state.

Instrument copied worker phases Queued, Running, Complete, collected/Idle and
exact session transitions. Advance the owner repeatedly without advancing the
worker; assert no filesystem call, sleep, blocking wait, or copied-result read.
Retain contexts until the real completion fence, including cancellation.

For every cut, assert no defaults, no automatic source merge or initialization,
no native root/pending loss, no lost retained custody, no stale permit acceptance,
and no cleanup of unrelated handles/artifacts. Where a checked native commit has
already linearized, assert its allowed complete old/new root and captured-prefix
semantics; do not assert cancellation can roll it back.

## 2. One-use permit proof argument

The proposed permit is a native dispatch slot with a selected device binding,
exact call kind, owner identity, attempt serial, one unused Sync credit and a
separate bounded raw-mailbox call sequence. It is not a public boolean.

Current X4 begin, async-submit, package-submit, collect and end wrappers each call
async_owner/is_owner exactly once before shared adapter dispatch
(minimal/drivers/x4pro_sd/driver.c:539–550). The first successful selected Sync
check consumes the credit. The permitted shared/native path performs only copied
metadata/mailbox operations; it invokes no app callback or scheduler. Therefore:

1. Outside that native dispatch interval, the selected Sync admission stays false.
   Non-ISR admission is checked explicitly, beyond current task-handle identity.
2. Between arming and the exact wrapper entry, there is no callback/yield through
   which another owner call can steal the unused credit.
3. After wrapper entry consumes the credit, a same-device reentrant ordinary call
   receives false even if the async session has since been cleared.
4. Other tasks never receive owner admission. Wrong attempt/device bindings cannot
   arm the slot. Unrelated devices keep only their preexisting independent rights.
5. Pending work receives copied request bytes and scoped worker I/O, never the
   dispatch credit. On every function return the slot is closed and zeroed.
6. Reentry into the controller fails before opening another slot. Cancellation
   does not interrupt the synchronous owner dispatch; it queues revocation and
   uses a later exact end dispatch, with its own bounded credit.
7. The native worker cannot use the owner slot. Native acknowledgement consumes a
   separate owner-issued receipt/pin, which grants no SD callback authority.

These statements depend on the exact verified provider/native call graph. Unknown
providers, changed wrapper behavior, unexpected second owner checks, or callbacks
inside the dispatch interval must be refused and reviewed. Friendship/private
constructors are trusted-code boundaries, not memory isolation against arbitrary
native corruption. A plain RAII boolean would not establish statement 3.

### Executable permit cases required before accepting that argument

| Injection | Required assertion |
|---|---|
| Each of five real async wrappers | Exactly one selected Sync credit consumed; no general owner privilege remains. |
| Return OK, PENDING, BUSY, INVALID, IO or RETAINED | Call slot/credit cleared before return to scheduler; retained state remains pinned. |
| Reenter controller before/after credit consumption | Nested call refused; outer token/credit unchanged, no second dispatch. |
| Inject ordinary selected volume/export/sleep/service calls after entry and immediately after End clears session | Every call refused without I/O or state mutation. Spent credit is not replenished. |
| Callback/yield hook between arm and direct wrapper entry | Build/fixture treats this as an invalid profile/path; no successful operation or published receipt. |
| Same-device second Sync check | False; exact wrapper either refuses or test rejects changed profile. |
| Wrong device, scope, provider grant generation, owner task, ISR context, attempt serial | Cannot arm/consume selected credit or issue native receipt. |
| Unrelated device call | Gains no extra rights; ordinary preexisting admission is unchanged. |
| Cancel while provider operation Queued/Running/Complete | No permit survives across polls; exact cancellation/end only; Running remains pinned. |
| End cancels old op then submits cleanup | Only exact old token and zero-operation cleanup allowed; minted cleanup token recorded. |
| End collects cleanup then resubmits cleanup | Bounded legal sequence only; no arbitrary package or raw card request. |
| Raw read/write/power/open/close, bind/unbind/release attempt inside async permit | Refused unless separately authorized for that exact lifecycle phase; Sync credit does not grant raw I/O. |
| Dispatch exits without consuming its credit | Refuse/invalidate the profile result; destroy the unused credit. |
| Stale copied request/result or replayed receipt | No cross-attempt/session/worker acceptance. |

No test may make is_owner permanently true or convert public UNAVAILABLE into
READY to reach the happy path.

## 3. Admission and bootstrap matrix

- Cold exact provider: graph/start succeeds without a card scan; repeated owner
  polls do not mount; one worker REFRESH mounts; end OK plus owner observation and
  native-scope check establish ReadyClosed.
- Start unavailable due to clean absent/unformatted/unreadable medium: no guessed
  absence or vacancy, no defaults; terminal checked result distinguishes clean
  unavailable from retained resource custody.
- Wrong/ambiguous native-selected provider, namespace, ABI/profile or grant:
  refuse without I/O or expanded authority.
- Held selected Sync lock, graph callback/reentry, earlier live session or queued/
  running/completed-uncollected mailbox: refuse/defer; never adopt or cancel it.
- Idle mailbox with somebody else's idle session: preliminary checks cannot claim
  session absence. Provisional fence plus exact Begin must return a clean refusal;
  restore ordinary admission without touching that session. An uncertain Begin
  result retains the pin; only a newly issued owned session commits admission.
- Prior read handle, write handle, directory handle and logger-owned handle:
  qualified warm begin refuses each; handles remain unchanged. Include ready()==
  true so REFRESH's shortcut cannot hide the handle.
- Warm unqualified current provider: Unsupported/Deferred. Fresh-exclusive mode
  must prove it published no competing grants or ordinary callbacks since start.
- Provider/native scope allocation, power, open/init, mount, close, unbind or
  release failure at every before/after boundary: retain on uncertainty; clean
  unavailable can retry only as a new later attempt with new epoch.
- Specifically inject native io->power failure with provider UNAVAILABLE and
  native fault=true: final native-scope evidence forces Retained.
- Bootstrap cancel before begin, queued REFRESH, actual running mount and completed
  uncollected REFRESH: drain exact ownership; no interrupted vendor-call unload.

## 4. Multi-consumer, lifecycle and diagnostics matrix

For every consumer and every queued/running/completed/idle owner boundary, attempt
ordinary selected-provider calls, a second async session, native raw scope calls,
USB PHY/export, sleep/wake/hold/rail changes and graph unload/reset. Repeat after
child end, between catalog readers/scans, after Verified and while native completion
is queued/running. The outer fence must remain effective when session==0 and the
mailbox is idle.

- Ordinary calls fail/defer before I/O; retained attempts retain, not merely busy.
- Provider service does no SD work while fenced. Do not rely on a UI convention
  that other callers will voluntarily avoid the provider.
- Sleep tests cover every actual native entry path, including light/deep, timed,
  wake-set and GPIO hold; reverse admission covers an already held USB/sleep lease.
- Graph release/shutdown is rejected before pending-release marking, consumer
  unpin, lease/stream revocation or quiesce. interfaceFor remains usable by the
  controller. Test last-grant and dependency/shared-grant cases.
- No descriptor diagnostic callback runs while the SD worker can mutate error
  fields. Specifically instrument ModuleV2::reportQuiescence and X4
  bootlog_descriptor_error. A deliberately failed fenced quiesce is not a valid
  normal-busy implementation.
- Sync unlock/destroy keep their documented cleanup behavior. Acquisition with
  a held token is refused so fencing cannot poison an in-progress guard_leave.
- App exit first cooperatively drains/delegates proven native-owned work; it does
  not turn ordinary pending work into permanent retention or unload borrowed
  buffers. Failed drain preserves exact contexts/mappings.

## 5. Owner receipt and native commit matrix

Use a private copied owner-issued receipt and a real completion pin. Bind action,
namespace, job, child-close sequence, exact verified identity/content metadata,
provider generation and attempt/mount serials. No public observe or SD call is
permitted on the native settings worker.

- Receipt before checked close/final observations: impossible/refused.
- Fabricated, modified, expired, wrong-action, wrong-core, duplicate or cross-job
  receipt: no native mutation. Historical Candidate/Report alone cannot issue it.
- Owner cancellation/revocation wins before worker claim: no native I/O.
- Worker claims Committing first: cancellation/USB/sleep/unload defer and pin stays
  valid until checked completion. Test both sides of this linearization; never
  promise to undo a published root.
- Native cancellation never calls the issued job's mailbox cancel or suppresses
  its result. Revoke an unclaimed receipt, let its callback return without native
  I/O, and collect it. If Committing already won, collect the actual root result.
- Same physical worker task dispatching NativeSettings versus ProviderIo: only
  the exact ProviderIo scope can invoke raw SD callbacks. NativeSettings cannot
  borrow that scope, rebind the provider mailbox or invoke public observe. The
  provider's on_worker callback also returns false during NativeSettings; inject
  ordinary provider calls to prove they cannot bypass Sync via valid_task or
  guard_enter. The fixed native job callback cannot be selected by app/card input.
- Native slot queued/running/complete-uncollected or retained while the provider
  mailbox is idle: exit/sleep/unload still defer or retain as appropriate. The
  old provider-mailbox-only idle predicate cannot release the native custody pin.
- Native preparation copies snapshot/marker/header budget under serialization;
  owner polling starts only after checked collection. Inject newer native edits
  after handoff: no shared mutable core read or consumer context race, and final
  native admission still checks the current revision/frontier/provenance.
- Older/newer local edit before snapshot, before receipt, queued before commit and
  after commit: native reads and all unacknowledged groups remain intact; exact
  captured frontier only is eligible for acknowledgement.
- Rebase/export overlap: single-ticket rules remain enforced; the receipt does
  not restore an invalidated ticket or override baseline provenance.
- Native write/readback/root failure, uncertain result and retained result: keep
  required buffers/provider/epoch pin and complete old/new-root recovery semantics.
- Unexpected custody loss during Committing: retain and reconcile actual native
  result; no false success or assertion that no native mutation occurred.
- Owner final observation becomes RETAINED after worker returns: do not release
  dependencies or expose ordinary completion; retain the actual native result.
- Replay after reboot: old receipts/epochs invalid. Durable marker recovery uses
  fresh verified SD evidence and current native provenance, never an imported
  marker/incarnation or a remembered media serial.

## 6. Session/error/memory matrix

- Real adapter end OK followed by final RETAINED on success, failure and cancel,
  including simultaneous lease loss; retain before exitSafe/output. Preserve the
  published terminal-hardening regression across all consumers.
- Both copied result status and collect return, terminal versus polling BUSY,
  invalid sizes, wrong tokens and repeated PENDING: no stale result consumption.
- Native transport snapshot only after true completion fence; test no owner reads
  of worker-mutated flags while pending and no false task identity on the worker.
- Ambiguous package/transport IO revokes further normal operations; drain. In
  particular, an ExistingPage IO must not silently authorize orphan deletion in
  the new native session path. Separate valid content-conflict behavior explicitly.
- No REFRESH/remount/power transition after evidence collection begins. Any
  observed serial/binding change revokes the attempt; no transparent refresh retry.
- Epoch/token counter exhaustion refuses instead of wrapping. Scope reset and
  address reuse cannot resurrect an old receipt.
- Sequential child readers retain bounded owned buffers and checked closes. Test
  retained buffers, wiping, allocation failure and total simultaneous RAM limits.
  Include catalog's large resident object and inventory peaks, native core, worker,
  DMA/hash/provider memory, allocator overhead and actual target stacks.

Run normal, UBSan and ASan where supported; keep LeakSanitizer qualification
separate. Include true owner/worker scheduling stress and race checks where the
host platform supports them. Run strict Xtensa/IDF object and composed target
builds after implementation; report linking/execution separately.

## 7. Evidence boundary

Existing separate normal suites were exercised against review baseline Runtime
7daed93b1837c1418531c9c95630136afb0d4ed7 during this source review (not a new
e471163 or proposed-adapter execution): Runtime storage-work-port, native SD checkpoint, native SD reader, NativeSdmmc
shim, and X4 minimal/scripts/test_storage_async.py against actual X4/shared FatFs.
They passed, but they do not include the proposed coordinator. The existing
replica fixture uses a boolean guard and READY during its own session. The actual
X4 test compiles the masked observer, but sd_async_test.c does not call observe_state;
that test does not exercise the observer incompatibility. Joining the real owner
observations and consumers without overriding that difference is the required
next proof. No new composed custody test has been implemented or claimed to pass
here.

Host/target-software evidence can establish exact authority, fences, epochs,
no-remount call paths, native root/frontier invariants, bounded memory, and failure
ordering. Review the pinned IDF source/binaries for implicit transport retry or
reinitialization; that is a software-evidence task, not inherently hardware-only.

Genuinely physical qualification still includes:

- SD controller/card durability after power loss, flush and checked close.
- Actual power rail, removal/reinsert/swap and contact-bounce behavior, especially
  between cached metadata operations and a later write; no-CD detection limits.
- Peripheral/DMA timeout and failed-close electrical custody.
- Real light/deep sleep, USB host and card-controller concurrency/settling behavior.
- Device heap/stack peaks, watchdog/latency and endurance under sustained use.

A CID, geometry match, READY result or passing sector-model test alone proves none
of those physical properties. State the accepted single-attempt failure model;
fail closed if its no-remount/fail-stop assumptions cannot be qualified. No new
persistent card credential or mandatory per-save confirmation is proposed.
