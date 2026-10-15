# Device-independent preferences

`storage.preferences@1` (`sdk/app/RiscPreferenceStoreV1.h`) exposes one opaque
namespace snapshot/get/submit/collect/cancel API. The native build enables the
broker for every profile through `idf/product-profile.cmake`; other builds may
explicitly select `RISC_RUNTIME_PREFERENCE_STORE=1`. The generic broker contains
no board identities, device names, application keys, or physical storage paths.

Boot policy binds an application to an exact positive namespace. A registered
NativeAtomicKeyValue writer is selected first. An existing native namespace with
no matching writer registration fails admission; it never drops into legacy KV.
Once selected, native Busy, Uncertain, Retained and other failures propagate
without fallback. When no native namespace is registered, the facade uses the
existing namespace-scoped KV backend and existing scalable/installed dispatch.
Native registrations and read-through restrictions remain unchanged.

The ABI reuses the atomic table/status layouts, not its blanket guarantees:

- COPIED_INPUT (4) guarantees inputs are fully copied before any backend write
  and none remain borrowed when submit returns. Accepted operations always need
  one terminal collect. The provider may finish the work synchronously.
- ATOMIC_GROUP (8) is set only for the registered native atomic backend. Its
  original native-durable/copied-worker flags remain set as well (total 15).
- The synchronous KV adapter advertises only 4. It supports PUT, validates and
  copies the complete group before I/O, and commits in supplied order. DELETE
  is rejected before I/O because the existing KV contract has no deletion API.
  Any backend failure after accepted submission gives an UNCERTAIN receipt,
  including first-write failure. Partial changes are possible; never auto-replay.
  A cancel after synchronous submit loses the completed-publication race.

Synchronous revisions are runtime namespace generations shared across grants.
Successful and uncertain writes through existing app and bound-provider APIs
invalidate old drafts. Busy legacy writes do not change revision. A namespace
operation holds its receipt until its exact grant collects it; other preference
reads/submits and legacy writes return Busy while it is held. The old synchronous
APIs, namespace identities, encoded values and NVS key names are preserved.
Revisions are not persistent IDs across reboot and cannot detect writes outside
the registered runtime backend authority. No migration, default policy, filesystem
acknowledgement, synchronous group atomicity, or extra durable journal is added.

Grant contexts are unique and owner/invocation-bound. Peer grants cannot collect
or cancel each other's tokens. Release, handoff, restart and invocation exit
require terminal collection. Native retained custody revokes authority and keeps
images/dependencies pinned. The facade remains draft-capable during native owner
maintenance checks through its original atomic endpoint registration.

Preserving cohort validation recognizes replacement of an existing KV/atomic
capability with preferences for the same application and namespace. It rejects
owner reassignment and namespace-range collisions. Existing legacy KV1-to-KV2
comparison rules remain intact. Runtime does not choose product mutation order.

Host qualification (no hardware claim):

- `bash test/run_preference_store_runtime_test.sh`: real Runtime sync backend,
  old writers, copied inputs, partial failure, uncertainty, cross-grant/context
  guards, receipt custody, plus all native atomic cases through the facade.
- `bash test/run_preference_store_cohort_test.sh`: same-owner legacy/native
  transition with no I/O, and new-owner rejection.
- `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` repeats these under ASan/UBSan.
