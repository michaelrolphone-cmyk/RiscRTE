# Experimental atomic namespace broker

This source increment does not enable a product profile. The compile selection
`RISC_RUNTIME_ATOMIC_KEY_VALUE` defaults to `0`; ordinary Runtime and Watch have
no atomic fields, registrations or callbacks. Existing SDK layouts and Port
layouts are unchanged. No firmware, product boot policy or device was changed.

## Native admission

With the selection explicitly set to `1`, trusted firmware can call:

- `Runtime::registerAtomicKeyValue(appId, namespaceId, endpoint)` before
  `prepare`, or from the compiled-in `bindPlatforms` callback.
- Optionally, `registerAtomicKeyValueReadThrough(namespaceId, endpoint)` during
  the same admission window.

The endpoint is a concrete `RiscExperimental::NativeAtomicKeyValue`, not an
application-supplied function table. Admission checks its immutable namespace,
clean native lifetime, positive namespace bounds and exact copied app identity.
Eight copied binding rows cover atomic app authorizations and read-through
selections. Different endpoints cannot be registered for one namespace. A
single endpoint can explicitly serve multiple app identities. Registration
alone gives no app authority: validated manifest requirements and exact
capability/API/namespace boot grants are also required. Missing admission,
wrong versions and ordinary provider attempts to impersonate the atomic
capability fail closed. No metadata constructs native endpoints.

The endpoint, core, namespace gate, backing store and scheduler must outlive
all Runtime use, including retained invocations. All users of the core,
including SD work and these optional readers, must honor the same gate. This
broker neither starts a scheduler nor qualifies private-mount provenance.

## Invocation and operation custody

The app gets a Runtime facade with a nonreusing opaque context. Only the active
invocation owner during app entry may call it; init/fini, foreign workers,
suspended resident invocations, released grants and copied stale tables reject.
Returned API pointers expire when their grant is released. Copied callback and
context pairs never regain authority. An atomic owner gate rejects foreign tasks
before inspecting mutable Runtime state. Instance zero resolves only an unambiguous namespace grant.

Successful submit copies the mutation group and explicit intent in the native
endpoint. The broker records the operation token against the exact grant.
Another grant cannot collect or cancel that token, even when it names the same
endpoint. Worker execution is native-only. A release attempt while an operation
is outstanding returns false and leaves the original grant callable for
collection. Cancelling still requires collecting the terminal receipt; a
committed success is preserved if cancellation loses the publication race.

Normal capability acquisition can coexist with pending copied work. Real app
exit, resident handoff and terminal reset require settled endpoint custody and
collected receipts. An attempted real exit with pending work pins the invocation,
revokes app/provider authority and skips fini/unload. Native `Retained` does the
same immediately. The optional shared native retention predicate also makes SD
or maintenance custody faults visible to state/get/submit and the exit barrier. There is no automatic replay, cleanup retry, cancellation or
receipt adoption after revocation. Destroying Runtime with unsafe atomic custody
aborts rather than silently releasing dependent memory.

## Optional existing-reader routing

Read-through is an explicit native namespace selection, separate from an app's
atomic-write authorization. Existing app KV1/KV2 namespace grants and exact
provider-bound key/access maps still apply. Existing backend presence and
per-version metadata admission remain required. For a selected namespace:

- Reads use the atomic endpoint's state/revision/get under its shared gate.
- KV1 keeps its 64-byte limit; KV2 keeps its 2048-byte limit. Oversized values
  report legacy IO, with no partial copy. Size probes and small buffers retain
  the existing exact required-size contract.
- Busy or an intervening stale revision reports legacy BUSY. Missing reports
  NOT_FOUND. Other failures report IO, with zero output size and no copied data.
  Native Retained also fences Runtime custody.
- Every legacy app or provider put returns CONTEXT after ordinary argument and
  authority validation. A legacy write never bypasses the core, falls back to
  old NVS/scalable storage or fabricates an operation intent.
- Other namespaces retain their original routing and write behavior.

Routing does not initialize, migrate, merge or discard either dataset. A product
must independently establish which native replica is admitted before selecting
read-through. Conflicts preserve both datasets. Native success does not imply
SD acknowledgement or survival of a destructive erase.

## Verification

`test/run_atomic_key_value_runtime_test.sh` exercises real Runtime/Graph and
loaded app/provider images with actual NativeAtomicKeyValue/Core. Tests include
exact admission, foreign task rejection, copied buffers, per-grant receipt
ownership, release/reacquire and handoff revocation, resident host/child scope,
cancellation before and after commit, queued and terminal-retained exit barriers,
and KV1/KV2 app/provider read-through with legacy-write denial.

`SYSTEM_ROOT=... test/run_runtime_atomic_wifi_controller_test.sh` additionally
executes the actual System Wi-Fi controller at
`a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89`, with only UI/radio adapters synthetic.
Atomic acquire/release use the real Runtime. It verifies Save, connected
Remember, queued cancellation, and two profiles after a fresh Core/Runtime
reload over preserved host-native disk. This is source-bound host composition,
not firmware-update, filesystem-format, radio or hardware qualification.

`test/compile_atomic_key_value_runtime_target.py PACKAGES OUTPUT` compiles the
actual Runtime.cpp against ESP32-S3 headers, off/on in default/cohort layouts.
It also emits Runtime/Port/CPU-Port size probes. The off layouts match base
`68e6cefc10f16fe82b6f365dfe5daffd7f768d72` exactly. This is compilation, not a
linked firmware or device test. Evidence is in `evidence/runtime-atomic-broker`.

Add `--stack-usage` to emit GCC's per-function local-frame reports alongside
the object files. With the pinned Xtensa GCC 8.4.0 compiler and `-Os`, both
selected layouts report 4,176 bytes for `NativeAtomicKeyValue::submit`, 64
bytes for the broker's `atomicSubmit` frame, and 2,176 bytes for
`Runtime::atomicLegacyGet`. The copied submission staging is intentionally
on the owner stack; worker mailbox storage is separate. These figures do not
include the calling application/controller, further callees, interrupt frames,
or a runtime high-water measurement. They must not be presented as a total
task-stack requirement or proof that a product fits. Production admission
still needs its complete selected call-chain and physical stack qualification.
