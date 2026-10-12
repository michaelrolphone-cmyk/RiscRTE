# Protected SD directory, recovery and session-boundary review

## Disposition and scope

This is a read-only source review, not provider qualification, hardware evidence or authorization to activate an experimental path. No provider, SDK or application code was edited. No tests, target execution, serial/device access, flashing or publication were performed by this review. Existing files in this evidence directory are separate work and are not incorporated as executed evidence here.

**Keep the current strict refusal.** The pending catalog implementation requires READY during polling and therefore refuses the reviewed X4 profile after its own async session begins. This report does not relax that gate, fabricate READY, change the meaning of UNAVAILABLE, or grant initial creation/adoption/cleanup authority. A future boundary-only observation contract is a qualification option with unresolved gates below.

Source verification time: 2026-10-11T19:40:43Z.

## Source identities and reference notation

Every `R/`, `D/` and `X/` citation below expands to the following absolute root. The companion `PROVIDER_SESSION_SOURCES.json` records each cited baseline source's absolute path, SHA-256, reviewed line ranges, tracking status and byte equality to the named HEAD.

- **R:** `/workspace/shared/native-replica-legacy-reader-d1b`, HEAD `70cf262a5fd06d53a4ddb0b26423d88ac6453f3c`
- **D:** `/workspace/shared/x4-shared-settings-090/repos/Drivers`, HEAD `281609a0d2ae514f107d2ef67b6e8b4d25c72fb8`
- **X:** `/workspace/shared/x4-shared-settings-090/repos/X4`, HEAD `c8004c5c8362b142ebb35a0e1c5d266532a11581`

The Runtime R checkout has published tree-identical equivalent `dfe7cf3851438b566e708d46223e2b561b8147cb`. Both its local reviewed commit and that published commit resolve to tree `e6aafa0b56b6048d773be6d2af3a7cc3ffca6cd5`. GitHub readers can recover every R source from the published equivalent; the per-file hashes below remain unchanged.

All listed baseline source files were tracked and byte-identical to their HEAD blobs at verification. This is a file-scoped statement; it does not assert that each complete checkout is clean.

The pending catalog is a separate, not-yet-frozen reference: `/workspace/shared/native-settings-catalog-d3a333/experimental/native_replica/CheckedSdSettingsCatalog.h`, on base `d3a3337d873f324cac1ebcd0efec12dbf5e950be`. Its strict polling gate was inspected at lines 62–76; final transfer checks at 55–60; post-end retention at 68–71. Its final content hash belongs to the parent's frozen implementation checkpoint and is deliberately not substituted with this report's observation-time copy. These line references identify the inspected pending version and should be reconciled if it changes.

## 1. Directory contract and async statuses

### 1.1 Admission and copied operations

The public directory entry is name[128], uint64 size and uint8 is_directory. The extension specifies that `dir_next=false` means EOF only if directory handle_error is zero, and a failed checked close retains provider ownership. [R/sdk/app/RiscStorageVolumeV1.h:11–23,54–64]

Async operations use copied requests/results and one exclusive session. The SDK requires the integrating provider to reject synchronous admission while session!=0; the adapter alone cannot enforce that on arbitrary provider callbacks. Directory callbacks must be checked explicitly: extension discovery does not validate every base `dir_open`, `dir_next`, `dir_close_checked` or `handle_error` callback. [R/sdk/driver/RiscStorageAsyncAdapterV1.h:7–18; R/sdk/driver/RiscStorageVolumeAsyncV1.h:19–33; R/sdk/driver/RiscStorageVolumeFsV1.h:32–35]

### 1.2 Open, next and close

- OPEN_DIRECTORY returns OK when a nonzero handle is acquired. If open fails, only metadata==FS_NOT_FOUND maps to ASYNC_NOT_FOUND; other metadata outcomes remain IO. Existing non-directory objects do not count as absent. An already-open adapter directory returns BUSY. [R/sdk/driver/RiscStorageAsyncAdapterV1.h:106–109]
- NEXT_ENTRY returns OK when the callback returns true. A false callback becomes END only when handle_error(directory=true)==0; otherwise it remains IO. No open directory means INVALID. END leaves the directory open. [same:110–113]
- CLOSE_DIRECTORY, and internal operation-zero cleanup, first abort-close any adapter-owned file, then checked-close the directory. Any failed close latches adapter retention and preserves ownership. No handles is an idempotent OK. [same:83–88]
- NEXT_ENTRY's payload is entry.name/size/is_directory. result.size is not the filename or entry length. Results are zeroed before execution, but pending output must not be consumed. Requests need terminated path storage even for operations without a meaningful path. [same:76–80,125,136–143]
- Package RECOVER rejects an open adapter file or directory with BUSY. A scan must finish and checked-close before a recovery operation is admitted. [same:34–35,73]

### 1.3 EOF, names and ordering in the actual shared FatFs provider

`dir_next` returns a complete 1–127-byte name with NUL, along with size and a normalized 0/1 directory flag. It does not truncate. A longer returned UTF-8 name latches FR_INVALID_NAME, so async reports IO, not EOF. Subsequent reads retain the directory error until a successful rewind or close. [D/lib/StorageFatFs/volume.c:285–318]

FatFs filters dot entries internally, but the generic volume ABI does not promise that every provider omits them. A defensive bounded inventory may tolerate only exact `.` and `..` directory entries and charge them to its work budget. Other dot-prefixed files remain artifacts. Enumeration is on-disk filesystem order, not a sorted-name guarantee. [D/lib/StorageFatFs/fatfs/ff.c:2312–2379,4710–4736]

This FatFs is configured with maximum 127 UTF-16 LFN code units, UTF-8 API output and a 511-byte intermediate LFN buffer. Invalid/unsupported LFN sequences can fall back to the short name or `?`. Such output is evidence of an object, not evidence of absence; the consumer must not reconstruct an assumed original name. FAT case/short-name aliases also mean that textual spelling is not unique physical-object identity. [D/lib/StorageFatFs/fatfs/ffconf.h:124–125,144–164; D/lib/StorageFatFs/fatfs/ff.c:2678–2735; D/lib/StorageFatFs/volume.c:431–462]

The provider accepts a bare absolute namespace path. Its path validation rejects non-root trailing slash, traversal, empty components, trailing dot/space and specified illegal characters. [D/lib/StorageFatFs/volume.c:194–213]

### 1.4 Missing directory and metadata

fs_metadata distinguishes FR_NO_FILE and FR_NO_PATH as FS_NOT_FOUND. Invalid arguments/path are FS_INVALID; not-ready media or failed guard release are FS_IO. Output metadata is populated only on successful stat. Missing an ancestor therefore also produces NOT_FOUND. [D/lib/StorageFatFs/fs_extensions.inc:22–34]

The adapter's open-failure fallback cannot turn arbitrary errors into absence, but a missing selected file still does not prove an empty namespace. The pending catalog goes further: bounded parent/root enumeration cross-checks directory-not-found. That is conservative evidence gathering, not a lease or physical-card check. The foundational plan requires bounded complete inventory, exact supported-name checks and preservation of unknown artifacts. [R/experimental/native_replica/ADOPTION_MIGRATION_PLAN.md:52–73; pending catalog:83–85,112–123]

## 2. Exact-name RECOVER is a mutation

The shared filesystem contract reserves `.risc-replace`, requires closed operands and says failure never authorizes deleting destination or recovery data. [R/sdk/driver/RiscStorageVolumeFsV1.h:3–16]

Actual `fs_recover_replace(destination)` appends `.risc-replace` and executes:

1. Backup absent (FR_NO_FILE/FR_NO_PATH): return success without mutation.
2. Backup present: require an ordinary file and free operands.
3. Destination absent: rename backup to destination.
4. Destination present as ordinary file: unlink the backup.
5. A relevant directory operand, failed filesystem call or failed guard release: failure.

It rejects root and a destination itself ending in the reserved suffix. It does not validate checkpoint magic, CRC, SHA, grammar or provenance. [D/lib/StorageFatFs/fs_extensions.inc:47–74]

Thus recovery of an already-present destination can delete backup evidence even when that destination's contents are invalid. Missing-only exact recovery preserves the distinct present-destination-plus-backup case. Only fixed supported destination names may be authorized; never generate recovery targets from arbitrary listed backup names. Failure may occur after a mutation took effect. [R/sdk/driver/RiscStoragePackageAsyncV1.h:8–13; R/sdk/driver/RiscStorageAsyncAdapterV1.h:69–73]

For order-independent before/after comparison, a permitted missing-destination recovery can replace exactly one supported backup name with its destination, preserving regular-file type and size. This is an inventory transformation check, not proof that those bytes were valid or unchanged. Successful child content validation/rechecks remain separate.

## 3. Bounded inventory requirements

- Use fixed admitted native capacity. Compare exact logical names, size and directory kind independently of enumeration order; do not compare struct padding or rely on an XOR-only digest.
- Validate NUL termination, nonempty names, separators and directory tags. Reject duplicate or ambiguous case-folded entries rather than silently coalescing them. Treat unrecognized names, pages, staging files, backup artifacts and subdirectories as non-vacancy evidence.
- Bound every returned entry and logical name byte, including tolerated dots. No budget exhaustion, malformed name, I/O error, incomplete EOF or failed close can establish vacancy.
- Separate present-empty from absent directory observations. Require complete before and after scans and allow only explicitly qualified recovery transformations. Unrelated additions, removals or type/size changes invalidate stability.
- Exact name/type/size equality is not content equality, durable provenance or permission to publish. The same exclusive protected namespace/custody lease must span scans, readers, recovery and transfer; later publication needs fresh checks under its live lease.
- Directory tell/seek positions belong to the same unmodified directory on the same mounted volume. They are not persistent scan receipts and must not survive recovery-induced directory mutation or media transitions. [R/sdk/driver/RiscStorageVolumeFsV1.h:5–8]

Baseline test-fixture limitations must not be confused with actual provider guarantees: R/test/native_replica_sd_fixture.h:39–82 has no directory callbacks, models only files in metadata, omits metadata size, constrains paths to a child of `/RTECFG/00000006/`, does not count RECOVER in mutations, and returns READY during sessions. A catalog-specific fixture must explicitly model directory errors/EOF/close and actual active-session observations. This statement concerns the hashed baseline fixture, not the parent's pending fixture changes.

## 4. Future boundary-only observation contract

### 4.1 Candidate contract, not current behavior

A potential unified contract is: observe before beginning and after a successful end; while owning the exact session, rely on copied collect/end outcomes plus an independently qualified native protected-namespace/mount-custody lease; never read unsynchronized worker flags or convert UNAVAILABLE to READY.

This is structurally compatible with the reviewed X4 fences. It is not yet a universal guarantee of the current state/async ABI. StateV1 says READY is only a snapshot and calls for recheck after serialized calls; async v2/v3 does not explicitly document the proposed substitution. Qualification needs an explicit documented provider profile or an appropriate additive contract. [R/sdk/driver/RiscStorageVolumeStateV1.h:2–11; R/sdk/driver/RiscStorageVolumeAsyncV1.h:19–33]

### 4.2 Completion fences

The native mailbox copies requests before release-publishing Queued. The worker acquires that state and release-publishes Complete only after the callback returns. collect acquires Complete before copying bytes. cancel either wins the Queued→Idle transition before execution or acquires completed work before returning CANCELLED. Running/PENDING is not a completion fence. [R/src/runtime/storage/StorageWorker.h:18–59]

The SDK adapter waits for collect/cancel acknowledgement before reading worker-mutated handles and retention. Worker result/status/size inconsistencies conservatively retain. end polls cancellation or worker cleanup, preserves session ownership while pending and clears the session only after final OK. [R/sdk/driver/RiscStorageAsyncAdapterV1.h:153–182]

The native worker task and buffers persist; a blocked vendor call is not killed or unloaded. PENDING/BUSY cannot justify dropping grants, even when a higher-level work budget expires. [R/src/ports/esp32s3/NativeStorageWorker.h:9–26; R/sdk/driver/RiscStorageWorkerV1.h:15–19,41–46]

### 4.3 X4 exclusions

For owner callers, valid_task requires no async session. The native worker uses its separate scoped admission. Shared lifecycle and volume guards are wired to these functions; owner synchronous filesystem/export/sleep/quiesce entry is rejected throughout the session, including gaps when the mailbox is idle. A second begin is rejected by session ownership. [X/minimal/drivers/x4pro_sd/driver.c:103–132,503–516,539–550; R/sdk/driver/RiscStorageAsyncAdapterV1.h:128–135]

The reviewed export paths additionally reject live handles and invalid lifecycle/media state; release explicitly rejects an active session; quiesce checks valid_task and live handles. [X/minimal/drivers/x4pro_sd/Export.h:21–34,65–74; X/minimal/drivers/x4pro_sd/driver.c:650–675; D/lib/StorageFatFs/volume.c:73–81,508–527,582–596]

Observe returns UNAVAILABLE while a session exists before it reads the worker-mutated provider state. This avoids treating a concurrent in-flight snapshot as READY, but also hides retention until the session fence or a copied status reveals it. [X/minimal/drivers/x4pro_sd/driver.c:552–570]

### 4.4 Terminal custody requirements

- Stop normal submission on terminal operation failure, lease loss or cancellation; drain the exact owned session using its permitted owner cleanup path.
- RETAINED from collect/end is sticky: preserve provider/grants/dependencies, expose no report/candidate, and perform no further app I/O or speculative cleanup retry.
- end()==OK means adapter operation/handle cleanup completed, not that provider transport custody is healthy. A read or RECOVER transport failure can produce IO, then end OK, then final observe RETAINED due to sdmmc_fault.
- Observe after every end OK, including failed/cancelled jobs. Retention outranks ordinary failure or simultaneous lease loss. Successful output additionally requires the final lease and READY checks.
- Cancellation cannot undo a completed RECOVER. Cancellation may discard its result while its mutation remains durable. Never infer unchanged media from cancellation.
- A provider-level end failure or unacknowledged completion cannot be rescued by polling cached READY or abandoning the ownership token.

[Sources: R/sdk/driver/RiscStorageAsyncAdapterV1.h:69–88,153–182; R/sdk/driver/RiscStorageWorkerV1.h:44–46; X/minimal/drivers/x4pro_sd/driver.c:267–269,552–570.]

## 5. Physical identity versus per-attempt continuity

These are different claims:

1. **Durable physical-card identity** identifies a card across insertions, resets or attempts. A CID can contribute to that identification but does not by itself prove uninterrupted custody, prevent modification, detect removal/reinsertion of the same card, or invalidate stale cache contents.
2. **Native mount/custody epoch** identifies a qualified in-process storage tenure. For one bounded attempt, such an epoch plus proven no-remount/exclusive ownership and fail-stop transport behavior may be sufficient without exposing a durable card identity. That sufficiency must be established by the complete binding and failure model; it cannot be inferred from the async serial or a READY snapshot.

The reviewed public interfaces expose no CID or mount/custody epoch: card_info contains only structure/sector sizes, capacity, clock and reserved; the worker I/O table has no identity query. NativeSdmmc configures `SDMMC_SLOT_NO_CD`. This does not claim that the vendor's private card structure lacks a CID, and absence of a public CID is not by itself a reason to reject every future single-attempt contract. [R/sdk/driver/RiscGpioSdmmcV1.h:14–18; R/sdk/driver/RiscStorageWorkerV1.h:25–36; R/src/ports/esp32s3/NativeSdmmc.h:60–74]

READY/observe use cached state rather than a dedicated fresh identity challenge. FatFs directory reads can use a cached sector window without disk_read. Async REFRESH also short-circuits if ready already returns true. Consequently, successful cached work does not prove physical presence at every instant. Owner guard leave may do incidental log work; the precise claim is that the readiness predicate does not establish physical identity, not that every ready invocation is universally I/O-free. [D/lib/StorageFatFs/volume.c:129–138,240–247; D/lib/StorageFatFs/fatfs/ff.c:1078–1098; R/sdk/driver/RiscStorageAsyncAdapterV1.h:103–105; X/minimal/drivers/x4pro_sd/driver.c:121–132,552–570]

The existing StorageGenerationTracker explicitly describes observed filesystem coherence, not proof against unobserved media changes. Its mount/mutation counters and quiescent bit must not be relabeled as a fully qualified media-continuity proof. [R/lib/hal/StorageGeneration.h:5–26,68–81]

Observed transfer failures do fail closed across the stack: native SDMMC latches fault on failed read/write/status; the CPU scope latches fault; X4 latches sdmmc_fault; shared FatFs marks io_failed and unmounted. Further ordinary transfers are rejected and final observation can report retained custody. This is relevant evidence toward a future single-attempt fail-stop proof, not proof of all removal/reinsert/cache interleavings. [R/src/ports/esp32s3/NativeSdmmc.h:79–100; R/src/ports/esp32s3/CpuStorageWork.inc:71–92; X/minimal/drivers/x4pro_sd/driver.c:267–269,423–429,464–474; D/lib/StorageFatFs/volume.c:140–191]

## 6. Unresolved activation gates

### A. Cold bootstrap and refresh

X4 async startup deliberately leaves mounted/card_ready false until its first worker REFRESH. A pre-begin READY requirement prevents that bootstrap. A future contract must define a separately authorized refresh-only bootstrap and when a new mount/custody epoch becomes valid. It must not convert generic UNAVAILABLE into absence or assume a prior epoch survives a remount. No refresh should clear a mid-attempt fault and silently continue the old proof. [X/minimal/drivers/x4pro_sd/driver.c:328–342,634–642; D/lib/StorageFatFs/volume.c:230–238]

### B. Preexisting handles and full lease ownership

X4 begin and generic adapter begin do not call provider-wide has_handles. Adapter end closes only its own file/directory fields. Therefore the qualified binding must rule out conflicting synchronous handles, other protected writers and unmatched provider transactions before admission. A session prevents new ordinary owner calls, not the existence of older handles. [X/minimal/drivers/x4pro_sd/driver.c:539–542; R/sdk/driver/RiscStorageAsyncAdapterV1.h:128–135,161–182; D/lib/StorageFatFs/volume.c:73–76]

Guard checks during pending work must be owner-safe and must not inspect worker-mutated fields without a fence. The current experimental Guard is a callback promise, not a production implementation of that promise. [R/experimental/native_replica/CheckedSdReplicaCheckpoint.h:17–20]

### C. End-to-end USB, sleep and lifecycle exclusion

The provider's own APIs have a session exclusion. Do not generalize this to every platform lifecycle path without a composed test. The CPU's storageWorkBusy is mailbox-busy, not session-owned; various platform sleep entry points use that narrower predicate, while restart/quiescence additionally check scope ownership. The protected lease/controller must close the between-operation gap across all actual USB export, sleep, reset/unload and remount routes. [R/src/ports/esp32s3/CpuPort.h:135–138; R/src/ports/esp32s3/CpuPort.cpp:51–89,715–725,771–779,827–832; X/minimal/drivers/x4pro_sd/Export.h:21–34,65–74,199–225]

### D. Provider and native-scope custody agreement

A final provider snapshot and native worker-scope cleanup must agree; a successful adapter end does not release the underlying native SD scope or clear every platform fault. Native workRelease refuses an open/faulted scope or a still-bound mailbox; providerStorageSafe reads worker fault only when the mailbox is not busy. Actual cold-open/power/close failure paths need composed qualification, including any native scope fault not fully represented by the provider snapshot. [R/src/ports/esp32s3/CpuStorageWork.inc:26–34,69–92; R/src/ports/esp32s3/CpuPort.cpp:51–67; X/minimal/drivers/x4pro_sd/driver.c:250–269,650–655]

### E. Continuity model and tests

Specify exactly which transitions invalidate the native epoch, which actor owns it, how no-remount is enforced, and how faults revoke publication authority. Test cached metadata/EOF, removal during vendor I/O, removal/reinsert or swap, no-op RECOVER, final close and final transfer. CID-only checks and session-counter-only checks are insufficient.

Required composed tests also include active-session UNAVAILABLE without READY fabrication; wrong/stale session and operation tokens; queued/running cancellation; repeated PENDING/BUSY; read/RECOVER IO followed by end OK and final RETAINED; checked-close retention; lease loss with late retention; export/sleep/quiesce attempts while running and between operations; prior handles; cold refresh; and post-end checks on success, failure and cancellation.

Existing mailbox/adapter unit tests provide useful coverage of copied buffers, delayed running cancellation and retained close, but do not qualify the complete X4 catalog/lease/USB/sleep path. [R/test/storage_async_adapter_test.cpp:23–67; R/test/storage_work_port_test.cpp:25–50]

## Final conclusion

The future fenced-boundary approach can be a sound direction if the above native epoch, exclusion, bootstrap and custody guarantees are explicitly implemented and qualified. It is not approved or enabled by this review. The pending catalog remains strictly refusing the reviewed X4 active-session profile; no report, vacancy, create/adopt authority or release permission may be manufactured to bypass that refusal.
