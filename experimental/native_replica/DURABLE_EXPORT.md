# Disconnected durable export and reboot recovery

This integration extends the core-only RTENR02 checkpoint described in
[EXPORT_MARKER_CORE.md](EXPORT_MARKER_CORE.md). It closes the deliberately
preserved volatile-ticket limitation in [ROUNDTRIP_PROOF.md](ROUNDTRIP_PROOF.md):
SD publication followed by reboot and newer native edits can acknowledge exactly
the old captured prefix while keeping the current complete map and newer intents.
The old `start()` path remains available with its existing volatile semantics.
No production coordinator or SDK/port routing selects either experimental path.

## Caller flow and custody

1. Load native state normally. V1 initialization and all ordinary reads/writes
   remain usable without SD or an entropy service.
2. Serialize native operations and acquire the exclusive lease for this physical
   SD medium. Invoke `CheckedSdReplicaCheckpoint::startDurable(core,volume,id)`
   on the admitted native worker. Supply a fresh native incarnation only for a
   v1 upgrade; its separate freshness contract is in the core format document.
3. The writer captures the complete image, computes the exact target digest, and
   atomically persists the marker via the checked native store. It performs no
   SD operation until this checked native commit succeeds. A failed/uncertain/
   retained preparation never authorizes SD publication or prefix removal.
4. Run the existing copied SD worker through immutable page/index publication,
   verification and checked close. New native `apply` operations may commit
   meanwhile; each carries forward the marker and original captured prefix.
5. While retaining the same exclusive medium lease, invoke `acknowledgeNative`
   on the native worker after writer state `Verified`. It atomically removes only
   the marked prefix. A canceled/failed writer leaves the durable marker selected.

A new writer, rebase receiver, or volatile acknowledgement cannot replace an
active marker. Cancel means stop/drain this worker; it does not mean the SD write
was unpublished, nor does it clear the durable record.

After reboot, or after a safely drained failed/canceled attempt:

1. Load the selected native root. Keep ordinary settings readable. Later local
   edits do not destroy the marker or give it authority over those newer groups.
2. Construct `CheckedNativeExportRecovery(core,digest,guard,quiescence)`.
   `Quiescence::otherWritersStopped` must attest that no previous queued, running,
   retained or unclosed writer can issue a future write. A cancellation request
   alone is insufficient. This includes older non-durable `start()` writers;
   Guard must exclude their still-queued/running operations before granting a new
   lease. This is an explicit trusted coordinator hook, not a
   product implementation supplied by these headers.
3. Under the same-medium exclusive lease, call `start(volume)` on the native
   worker and drive the copied SD reader with `step()`. It validates full map
   grammar, digest, identity, unchanged index, and checked close. It then destroys
   the verified map, retaining only metadata and the selected native marker.
4. If state is `Verified`, inspect `decision()` and call `completeNative()` on
   the native worker while retaining both lease and prior-writer quiescence.
   Exact target yields `AcknowledgePrefix`; exact previous base yields
   `RetireUnpublished`. The latter clears only the marker and requires recapture
   of today's map before another export. Other/missing/corrupt identities refuse.

Recovery never replays an obsolete intermediate Wi-Fi Save over a newer native
Save. It never reads an old map from an inactive native bank to gain authority.
Equal generations, SD status flags, cancellation, or an arbitrary serialized
marker do not substitute for the exact selected-root handle and checked evidence.
Retained is sticky and `exitSafe()` stays false. The caller must keep all owning
objects/resources alive until a proven reset or safe custody resolution.

After `Verified`, transfer, cancellation and native completion observe provider
retention before releasing custody. A simultaneous lost lease or lost writer
quiescence cannot downgrade `Retained` to ordinary conflict or cancellation.
This precedence applies to the ordinary reader, recovery wrapper, and both
volatile/durable checked writers. The state observer is a metadata-only call;
no additional filesystem or native mutation is needed to latch retention.

## Capacity and compatibility

The reader now observes the selected header at `start`, so a reader constructed
before v1-to-v2 preparation cannot retain an obsolete import budget. The generic
held receiver uses `Snapshot.headerSize()` for both preserved group bytes and
full-image admission; each complete staged batch still checks the same bound.
The core remains the final authority and Runtime contains no Wi-Fi schema.

There is one private read-only recovery exception: verification may allocate at
most the SD format ceiling of 65,512 bytes even if today's native import budget
is smaller. A valid prior v1 base can be 65,360 bytes while a local deletion makes
v2 preparation possible with its 65,124-byte map-plus-groups budget. Recovery must
still verify that old base before retiring the marker. This exception does not
expose an import candidate and cannot install that oversized old map. Ordinary
reader/import admission retains the current native-envelope bound. Allocation
failure preserves the marker and complete native replica.

## Reproducible qualification

System behavior is pinned to commit
`c564dd665072db141fc42d55be3be52511750719`. The runner rejects a different HEAD or
modified relevant sources. In this execution environment its clean checkout is
`/workspace/shared/wifi-stage-pinned-c564dd6`.

```
export SYSTEM_ROOT=/path/to/clean/System-at-c564dd665072db141fc42d55be3be52511750719
bash test/run_native_durable_export_test.sh
bash test/run_native_export_retained_test.sh
SANITIZE=1 SANITIZERS=undefined bash test/run_native_export_retained_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address bash test/run_native_export_retained_test.sh
SANITIZE=1 SANITIZERS=undefined bash test/run_native_durable_export_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address bash test/run_native_durable_export_test.sh
bash test/run_native_rebase_roundtrip_test.sh
SANITIZE=1 bash test/run_native_rebase_roundtrip_test.sh
bash test/run_native_rebase_wifi_stage_test.sh
for suite in core store sd sd_reader; do bash test/run_native_replica_${suite}_test.sh; done
bash test/run_native_rebase_candidate_test.sh
bash test/compile_native_replica_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR
TARGET_CXX=/path/to/xtensa-esp32s3-elf-g++ bash test/compile_native_rebase_target.sh
```

The actual checked AppData/NVS adapter, core, copied native/SD workers, verified
reader, app-owned Wi-Fi Stage, held receiver, and writer are exercised together:

- 149 keys and 33 surviving networks; eight original intents with multiple
  Save/Forget operations; newer native Wi-Fi overwrite and opaque edit before
  publication, before live acknowledgement, and after reboot.
- 680 SD publication returned-error/power-cut cases; 165 cancellation, lease-loss
  and reset handoffs, each retaining the complete current native map.
- 164 recovery-reader failures, including 10 retained closes; 412 recovery
  cancellation, lease-loss, lost-quiescence and reset handoffs.
- 1,068 checked native arm/retire/prefix-ack failures, including 24 retained and
  54 uncertain outcomes; complete old/new root recovery and exact pending prefix.
- A 65,360-byte old v1 map verified for retirement while ordinary v2 import refuses;
  exact v2 receiver byte admission, one-byte excess refused before RAM staging.
- 30 focused late-retention cases: four reader transfer/cancel transitions, eight
  volatile/durable writer completion/cancel transitions, and 16 target-prefix or
  old-base recovery transitions, plus two internal reader transfers immediately
  after checked close. Lost lease/quiescence plus provider retention
  remains sticky, with no candidate transfer, native commit or extra I/O.
- Repeated prefix acknowledgement is stale. Foreign/newer/equal-generation but
  different-content SD, missing proof, lost lease, lost writer quiescence, and
  cancellation preserve the marker and all local intents.

The earlier full-chain fixture is shared without changing its assertions. Its
volatile-ticket limitation remains an intentional regression test for `start()`;
this test uses explicit `startDurable()` and the new recovery path.

All credentials and incarnation material are synthetic test data. This proof does
not implement entropy provisioning, first-dataset creation, fresh/legacy native
baseline provenance, migration, product lifecycle coordination, hardware custody,
production activation, SDK/UI selection, or hardware qualification.
