# Held native rebase receiver checkpoint

**Disconnected, native-only prototype.** `NativeRebaseCandidate.h` adds no SDK
ABI, public routing, Wi-Fi schema, production selection or `.90` activation.
Neither `NvsScalableSettings.h` nor `AppDataFiles.cpp` is changed.

This increment depends on `NativeReplicaCore::Snapshot` and `captureSnapshot`
from Runtime `7f750f82a47316ac4cb3760cc8dd665fcdd339bf`. Snapshot must expose `imageSize()` in
addition to namespace, map, count, base and ticket. Runtime's core remains
schema-blind. Actual app-owned Wi-Fi reconciliation is exercised only by an
explicit cross-repository test input (System Apps
`c564dd665072db141fc42d55be3be52511750719`); the receiver header never includes or
interprets Wi-Fi types, names, credentials or intent bytes.

## Authority and caller preconditions

A trusted native owner constructs the receiver with its bound core and an
already authorized complete namespace. Remote and batch namespace fields are
checked against that binding; their numeric values do not grant authority.
This is not an app-facing capability/ABI. Provider-scoped access must not use
this whole-namespace constructor as an expansion of an exact-key grant.

`begin` accepts an **already fully verified remote map and identity**, supplied
by the trusted transport/import owner under its held SD snapshot lease. The
caller must verify complete checked reads/close, map identity/content binding,
lineage and selected import/adoption authority before invoking it. The receiver
checks encoding, duplicate keys and native/remote identity compatibility, but
cannot establish that the bytes actually came from the claimed SD checkpoint.
A high generation by itself confers no authority. An unknown native base never
automatically wins or loses to the incoming generation. An unknown remote
identity is refused. Known native bases refuse foreign lineage, rollback and
same-generation divergent digest.

The SD exporter currently has no verified incoming importer. Passing its
identity checks is not permission to wire an unverified source into this API.
Checked import, initial adoption/migration, product authority, resource custody
and device qualification remain activation gates.

All calls and bound core operations run on one native owner under a serialization
lease; allocator/backend callbacks must not reenter or retain request pointers.
The receiver's reentry guard refuses nested calls. This is not a thread-safe
mutex implementation. A core object and its allocator/backend outlive their
receiver. A view/target callback cannot outlive its receiver object.

Rebase ownership and in-flight export/acknowledgement ticket ownership are
mutually exclusive until finish or abandon. `captureSnapshot` replaces the
core's single live ticket, including when `begin` later fails admission. An old
export acknowledgement then returns `Stale` without losing pending groups. The
fixture verifies this behavior. Capturing a rebase while an asynchronous export
owns that ticket is prohibited by the coordinator contract; this checkpoint
does not implement the coordinator or a second ticket namespace.

## Lifecycle and atomicity

1. `begin` copies the verified remote map and every captured original native
   intent into owned RAM. It retains the captured native revision/frontier and
   exact original group byte total. The caller can release input buffers after
   return. No input pointers are retained. Native storage is not written.
2. `get` and `pending` require the current held view revision. Pending enumeration
   exposes copied IDs/intents; it does not consume them. The native complete
   replica continues to serve its ordinary reads independently.
3. `stage` requires bound namespace, matching held-view revision AND pending
   frontier, unchanged native revision/frontier, the **next exact captured ID**,
   and byte-for-byte matching original intent. Unknown IDs, out-of-order IDs,
   duplicate resolutions and mismatched bytes are refused. Merely being below
   the frontier is never membership evidence.
4. The whole generic PUT/DELETE batch is validated and size-admitted before one
   replacement RAM allocation. The next state's header carries the resolved
   prefix count, alongside its complete map. A single owned-buffer swap exposes
   both map and marker, with no failure point or callback between them. Failed
   staging leaves the previous complete candidate and marker intact. A
   zero-mutation group still advances the prefix and view revision.
5. A successful batch invalidates earlier view revisions. Subsequent app
   resolution gets a fresh read capability over the newly staged map. Thus each
   Save/Forget sees prior resolved operations without touching native storage.
6. `finish` refuses unless **every captured pending group** was resolved in
   order. It checks the live native revision/frontier/count again, creates
   bounded map descriptors and ordered original IDs, and calls
   `core.installResolved`. Core repeats revision/ordered-ID admission, preserves
   original pending groups and uses its verified old-or-new native root switch.
   Newer native edits always make the candidate stale rather than being lost.
7. `abandon` wipes and releases candidate/intent RAM without changing the native
   root, pending groups or SD. Success or ambiguous finish is terminal; a later
   abandon does not claim to undo it. A fresh begin on the same receiver issues
   a new view revision, so an old batch cannot match a replacement candidate.

The app Stage-to-generic-batch bridge exists only in tests. `Stage::Committed`
from that bridge means **staged in this held RAM candidate**. It does not mean
native root durability, even though the standalone app Stage test model used an
atomic receiver terminology. Only receiver `finish == Core::Ok` establishes
native publication. Nothing here establishes SD durability or acknowledgement.

## Recovery and failure semantics

Resolution markers are intentionally volatile. They are neither durable native
metadata nor SD acknowledgements. A reboot/cancel before finish discards the
candidate, leaves the old readable native replica and all original intents,
and starts fresh app-owned resolution from a newly verified remote snapshot.

A successful native finish persists the complete candidate plus **unchanged
original groups**. Reboot does not infer that SD received them. Pending groups
remain until the separate transport validates the resulting exact SD checkpoint
and calls native acknowledgement. If SD has not advanced, resolving the retained
original intents again against its verified base remains the recovery path.

Allocation, validation, capacity and ordinary pre-root backend failures do not
publish partial native roots. A returned pre-root `Io` keeps the candidate and
can be explicitly retried if the core is still ready and its revision matches.
`Uncertain` during root publication/readback discards the candidate and its RAM
markers, keeps the terminal uncertain outcome and requires native reload plus
fresh remote acquisition. It is never translated into retry-safe `Busy` and
cannot be turned into a successful cancellation.

`Retained` from the backend is also terminal, and remains latched for the entire
receiver lifetime. Candidate RAM is wiped, but begin/stage/finish/late abandon
continue returning `Retained`. The native job/backend and their lease/context
must stay alive because a descriptor may still be owned. Discarding RAM or
constructing another receiver is not a resource release or safe reset. The generic
core also preserves `Retained` returned by root replacement or root readback,
while marking its native state uncertain/unreadable. The copied job therefore
cannot misclassify an ambiguous retained root outcome as exit-safe. Tests cover
retention before/after root replacement and during root readback, followed by
complete old/new recovery and unchanged original intent membership.

This increment additionally hardens **only the experimental**
`CheckedNativeReplicaStore`: retained AppData custody is checked before all
filesystem/NVS admissions and latched independently. Direct backend reuse
cannot advance the NVS allocation sequence after retained close. Its cleanup
routine only wipes independent RAM; it does not release retained native custody.
The test injects an actual AppData close failure during receiver finish through
the copied native job, then verifies receiver/store/job retained status and zero
further filesystem or NVS calls on retry, abandon, rebegin or direct backend
reuse. Production `AppDataFiles.cpp` remains byte-identical.

Native storage uncertainty
still means native reads may be unavailable until reload; this code does not
invent defaults or promise fallback after underlying corruption.

## Bounds, memory and sensitive copies

Limits cover complete native image bytes, captured pending-intent bytes and
canonical batch bytes. The complete-map admission includes the exact original
pending **group** bytes (including prior mutation payloads), header and trailer.
The default native image limit is the checked adapter's 65,512 bytes. A stricter
receiver budget is allowed; the receiver limit must not exceed the bound core
and backend budget. Core performs authoritative admission again at finish.

There is no fixed key, profile or pending-group count limit. Individual values
retain the core's 2,048-byte limit and keys its existing 15-character grammar.
Batch accounting is 32 bytes plus intent bytes plus 24 bytes per mutation plus
value bytes; defaults allow 4,096 bytes and 1,024-byte intents. This is a private
in-process admission representation, not a frozen wire or SDK structure layout.

The receiver holds one complete staged map and a compact original-intent copy.
During staging it additionally holds one replacement map. Begin temporarily
holds a complete core Snapshot; finish temporarily holds map descriptors and
ordered IDs while the core allocates/publishes another complete image. All are
explicitly byte bounded but may overlap core/backend buffers. Allocators must
return suitably aligned memory for descriptors and IDs. The target port must
measure full peak memory, PSRAM/internal-RAM placement and native worker custody.

Owned buffers are explicitly wiped before release even with a non-wiping custom
allocator. Test credentials are synthetic. No credentials or opaque intent bytes
are printed. These wipes do not prove physical flash erasure, encryption,
register/stack erasure or crash-dump sanitization.

Like the core prototype, map copying and duplicate scans are synchronous and
linear/quadratic. The source Stage bounds individual scan steps, but this receiver
is not a measured owner-poll latency, incremental flash-wear or device design.
Run it only as disconnected native experimental work until the actual worker
and transport lifetime contract is connected and qualified.

## Reproducible verification

In a combined Runtime checkout containing this receiver and the Snapshot API:

```
./test/run_native_rebase_candidate_test.sh
./test/run_native_replica_store_test.sh
SANITIZE=1 ./test/run_native_rebase_candidate_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined ./test/run_native_rebase_candidate_test.sh
SYSTEM_ROOT=/path/to/System ./test/run_native_rebase_wifi_stage_test.sh
SYSTEM_ROOT=/path/to/System SANITIZE=1 ./test/run_native_rebase_wifi_stage_test.sh
SYSTEM_ROOT=/path/to/System ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined ./test/run_native_rebase_wifi_stage_test.sh
TARGET_CXX=/path/to/xtensa-esp32s3-elf-g++ SYSTEM_ROOT=/path/to/System ./test/compile_native_rebase_target.sh
```

`CORE_ROOT=/path/to/Runtime-with-Snapshot` optionally selects the parallel core
checkpoint while this receiver remains in a separate worktree. `SYSTEM_ROOT`
must contain the actual frozen `experimental/wifi_reconcile/WifiStage.h` and its
PortableApps/sha2 dependencies; no copied/mock Wi-Fi resolver is substituted.
Leak detection is disabled because the execution runtime does not support it;
AddressSanitizer and UBSan still run. Target checks are single-job strict C++17
Xtensa GCC 8.4 objects with exceptions/RTTI disabled. They do not link a device
firmware, select an SDK table, flash hardware or qualify hardware behavior.

Executed evidence covers:

- 1,025 opaque keys; exact ordered IDs/intents; same-value and zero-mutation
  resolutions; generic PUT/DELETE; namespace/view/frontier/lineage refusal;
  copied input lifetime; callback reentry; all-resolved-then-concurrent edit;
  allocation/byte/storage exhaustion; custom allocator wiping; abandon/reboot.
- 54 before/after/torn returned-failure and simulated immediate-power-cut cases
  across a multi-chunk native finish. Reload yields either the complete old map
  or the complete new map, always retaining every original intent.
- Actual app-owned Stage and codec against the actual native core: seven ordered
  Save/update/Forget/reuse/already-applied/absent operations, offline slot
  remapping, remote count retention, cancellation, native concurrent edits,
  RAM allocation/capacity refusal and logical conflicts. Each accepted Stage
  transaction leaves the native root unchanged until finish.

The existing checked-store suite also reruns all 184 reached fault cases, plus
the new retained receiver/store/job no-further-I/O regression, under normal,
UBSan and ASan/UBSan configurations. Six same-receiver uncertain recovery tests
reload old/new native outcomes, reject pre-failure view revisions and successfully
re-resolve/finish every preserved intent. A previously acknowledged ID with
matching bytes below the frontier is explicitly rejected as absent membership.
