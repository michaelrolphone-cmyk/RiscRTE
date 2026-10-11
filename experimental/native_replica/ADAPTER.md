# Checked native replica adapter checkpoint

**Experimental, disconnected from every production boot/routing/ABI path.**
The existing `AppDataFiles.cpp` is byte-identical to Runtime 3b90. No partition,
NVS initialization policy, app namespace quota or `.90` selection is changed.

## Storage budget and ownership

The delivered partition table gives NVS 0x6000 (24 KiB), and the separately
provisioned AppData LittleFS partition 0x80000 (512 KiB). Putting the prototype's
1 MiB image in NVS is impossible. This adapter keeps only a 48-byte root and a
24-byte never-reused sequence record in a private NVS namespace `rnr%08x`.
Those 72 bytes are payload, **not total NVS overhead or reserved capacity**.
Existing NVS data remains untouched; initialization/space failure returns an
error, never erases, reformats or retries initialization.

Complete native images use the actual checked AppDataFiles replacement path in
two fixed files per settings namespace: replica-a.bin and replica-b.bin. Each
file has a 24-byte identity/length/CRC envelope, leaving **65,512 bytes** for the
complete map plus its pending groups. This is an explicit byte limit, not a
profile count or sixteen-key ceiling. The fixture stores 1,025 small keys.
Larger incoming maps and pending queues fail with NoSpace before publication;
the old readable replica and all pending edits remain intact.

The two files fit the existing 128 KiB committed namespace quota; one replacement
stage can additionally consume 64 KiB. Before reserve, a worker-only space hook
must report the exact 512 KiB volume geometry and actual used bytes. Admission
requires the full block-rounded candidate plus an explicit 32 KiB recovery
reserve to remain free. The caller must hold the global native storage lease so
another writer cannot consume that checked headroom. Quota is not reservation:
actual filesystem/NVS failure still preserves the old selected image.

For selected namespaces 1 and 6 at maximum bank sizes, four committed banks use
256 KiB, one serialized stage adds 64 KiB, and the reserve is another 32 KiB.
That leaves at most 160 KiB for existing app files and other overhead. Do not
assume this space is available on a deployed device; measure before migration.
The adapter does not format, grow or steal another app's allocation.

The caller MUST configure its AppDataFiles instance on a firmware-private root
(such as `/appdata/.rtecfg`), separate from public `/appdata/n%08x` files. The KV
namespace is constructor-bound and cannot be supplied through an app request.
The private root must be provisioned/checked by native port code. This checkpoint
does not add that mount/provisioning integration or grant applications access.

## Atomic selection and two-bank reuse

1. A first initialization writes a checked `RTENINIT` root before any bank data.
   This distinguishes a never-published initial candidate from loss of a real
   root. Missing root metadata with existing bank files is IO, never permission
   to initialize defaults.
2. Reserve chooses the inactive, unpinned physical bank. It durably advances and
   checks the NVS sequence before accepting any candidate bytes. The logical
   object ID combines that never-reused sequence with the bank bit. A failed
   metadata update is reread before any subsequent allocation.
3. Core chunk writes only populate owned native staging RAM. The new internal
   `seal` call invokes checked AppData replacement, followed by another complete
   checked read and exact comparison. No root can select an unsealed candidate.
4. The core verifies the sealed object in bounded chunks, then publishes its
   48-byte root through nvs_set_blob, nvs_commit and a separate read-only handle.
   Ambiguous root publication requires reload; it is not a retry-safe BUSY.
5. Every bank read validates logical ID, length and CRC. Reusing a physical bank
   cannot make an old logical ID resolve to new bytes. An export lease pins a
   specific bank/ID; attempting to recycle it returns Busy without allocating a
   new sequence. A stale unpin token cannot release a newer lease.

NVS's supported power-loss model preserves existing key/value data except an
update in progress; see the [ESP-IDF NVS guide](https://docs.espressif.com/projects/esp-idf/en/v4.4-rc1/esp32/api-reference/storage/nvs_flash.html).
The adapter assumes an old-or-new atomic metadata record and checks exact
readback. Its host NVS model tests API failures before/after persistence; it is
**not** the vendor flash implementation. The target headers used for compilation
are ESP-IDF 4.4.7, bundled with Arduino-ESP32 package
3.20017.241212+sha.dcc1105b. A real NVS page/GC power-cut campaign is still required.

## Bounded owner work, custody and wiping

`NativeReplicaJob` reuses the existing copied native Worker for control only.
The owner copies at most 4096 request bytes and polls/cancels nonblockingly;
filesystem and NVS calls execute only in the fixed native callback on its worker.
The adapter additionally requires a worker-plus-exclusive-lease guard. Polling
1,000 times before worker execution performs no storage I/O in the fixture.
Queued cancellation wipes request bytes and starts no write. Cancellation after
execution cannot undo a commit. Retained-close status survives cancellation and
keeps exitSafe false. The native job/context must remain alive until quiescent;
retained custody is preserved until reset, never destroyed as an app object.

Core-owned default-allocator buffers, adapter staging/cache, metadata scratch,
core readback scratch and copied request payloads are explicitly wiped. The
existing production AppDataFiles verifier has a **512-byte stack wiping gap**.
The candidate fix is saved only as `appdata-verifier-wipe.patch`; it is NOT
applied. The adapter uses a wiping allocator hook for the helper's heap snapshots.
These measures cannot erase SDK/flash copies, registers, crash dumps or physical
remnants. No real credentials occur in tests or logs.

The present reference implementation still rewrites one whole image per change.
It can simultaneously hold active/candidate images, staging/cache and the
helper's temporary snapshot (roughly five 64 KiB buffers), plus worker/control
and stack storage. Arrays containing Worker atomics must be native internal RAM;
large payload/image allocations can use suitably owned PSRAM. Measure the final
port's memory and flash wear before activation. This host checkpoint does not
claim target timing, storage lifetime or UI-latency qualification.

## Executed evidence and remaining gates

- Core normal/UBSan: 1,025 opaque keys and 270 deterministic injected returned-
  error / immediate power-cut cases, including staged-object sealing.
- Adapter normal/UBSan: 1,025 small keys; initial creation/retry; 184 actually
  reached API/chunk fault cases; before/after metadata updates; write/partial
  write/read/fsync/close/rename failures; complete old/new recovery; preserved
  pending edits; larger-map refusal; recovery reserve; namespace separation;
  monotonically allocated IDs; stale bank reads; export pins; copied worker
  cancellation and retained custody.
- Experimental headers compile as an Xtensa ESP32-S3 object with GCC 8.4.0,
  ESP-IDF 4.4.7 NVS headers, exceptions/RTTI disabled and warnings as errors.
  This is compilation evidence, not a linked/running product.

Still missing: native port worker/resource-lease and private-root wiring; actual
LittleFS/NVS flash cut tests of this adapter; verified real free-space reporting;
SD transport and restart acknowledgement; app-owned Wi-Fi semantic reconciliation;
final migration/format compatibility; public versioned ABI; hardware tests.
All remain outside `.90`. Do not enable shared native fallback based solely on
this checkpoint.
