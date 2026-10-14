# Source-bound full settings SD roundtrip

`run_native_settings_sd_roundtrip_test.py` qualifies a host composition of the
ordinary Settings and Wi-Fi controllers, checked native persistence, normal SD
checkpointing, complete simulated native erasure, and typed SD restoration.
It does not enable or qualify a firmware/product persistence policy.

## Reproduce

Run with a fresh evidence directory:

```sh
python3 test/run_native_settings_sd_roundtrip_test.py \
  --x4 /path/to/X4-at-c7404623 \
  --drivers /path/to/Drivers-at-281609a0 \
  --reference-reader /path/to/Reader-at-45cf61ac \
  --settings-system /path/to/System-at-5636756 \
  --wifi-system /path/to/System-at-a5bd60f \
  --output /path/to/fresh-evidence
```

Add `--sanitize address,undefined` for the combined ASan/UBSan run. If the host
rejects LeakSanitizer process inspection, use `ASAN_OPTIONS=detect_leaks=0` and
record leak checking as unqualified; this does not disable ASan/UBSan checks. The runner
rejects changed pinned inputs, records every selected transitive System
compiler dependency, hashes Runtime/test inputs and binaries, and refuses to
label a run complete if Runtime/test inputs changed during execution.

## What crosses the whole path

1. An explicitly synthetic matching empty native/SD baseline at generation 19
   is seeded as a test-media precondition. This setup is not first-boot source
   admission and is not counted as typed restoration evidence.
2. The actual pinned Settings controller makes seven preference Saves through
   actual Runtime atomic capabilities. The original fixture also exercises
   stale-draft rejection, queued cancellation, and cancellation after a native
   write starts. The actual Wi-Fi controller saves profile A and remembers
   profile B after its synthetic radio reports Connected, including the
   existing in-flight and queued-cancellation checks.
3. `NativeSettingsCoordinator`, `NativeAtomicKeyValue`,
   `CheckedNativeReplicaStore`, `AppDataFiles`, and `NvsReplicaMetadata` create
   the native values and pending intents. Runtime KV1/KV2 app/provider readers
   verify the real native read-through without legacy backend fallback.
4. `NativeSdSynchronization` durably checkpoints the pending values using an
   exact persisted export marker and a validated
   Runtime provider graph, real `CpuPort` custody and worker tables, the pinned
   actual X4 driver and FatFs, and synthetic sector hardware. No ready token or
   custody result is fabricated. Atomic edits, native preparation/completion,
   checked catalog work, and checked restore use the shared coordinator gate.
5. Both namespace 1 and namespace 6 checkpoints share one SD sector image.
   Separate executables preserve the exact Settings `5636756...` and Wi-Fi
   `a5bd60f...` dependency closures instead of mixing their System revisions.
6. The runner inventories and deletes every native AppData file plus both
   simulated-NVS state witnesses. The old processes and their in-memory NVS
   models are gone. Each restore starts with empty native files and empty NVS.
   The SD image is retained unchanged.
7. `CheckedSdSettingsCatalog` produces a typed, verified restore candidate under
   real SD custody. `CheckedSdNativeRestore` and `CheckedNvsFreshSources`
   inventory the empty native destination and restore through the checked
   store. No raw-map initializer is used on this restore path.
8. The restored core/store/coordinator are destroyed and reconstructed. Fresh
   actual controllers and Runtime app/provider read-through readers verify
   exact preferences and both complete SSID/password pairs. Serialized native
   map bytes match the pre-checkpoint expected bytes exactly.
9. An unrelated `/ordinary.txt` is read with successful open/read/close before
   and after custody. Files-style opens and USB export admission are blocked
   while custody is held; USB export/read/end succeeds after terminal release.
   The entire SD image remains byte-identical through both typed restorations.

## Test seams and limits

- Initial matching empty native/SD baseline, sector hardware, UI/navigation,
  radio events, RTC/time providers, ESP-IDF NVS calls, and partition accounting
  are explicit host fixtures.
- Sector-image load/save is a test-only transport imaging hook. It runs before
  mount or after checked terminal release; checkpoint and restoration continue
  to use the actual provider/Runtime/CpuPort/FatFs operations.
- Namespace fixtures run sequentially with separate exact System closures.
  This does not claim simultaneous two-service scheduling in a target binary.
- Each build runs two complete campaigns. The second pauses the native worker
  before SD publication finishes, cancels the queued acknowledgement after the
  real SD index is published, and confirms that the selected durable marker and
  pending intents remain. It then discards/recreates the native core, store,
  coordinator, worker, and sync controller, reloads checked native persistence,
  and requires `AcknowledgePrefix` recovery before full native erasure/restoration.
  Both campaigns retain controller-level queued/in-flight cancellation coverage.
  This is a clean interrupted-boundary restart, not process-death power-cut
  coverage. Lower normal synchronization and checked restore suites cover their
  separately recorded durable/power-cut cases.
- No actual Files app, target firmware, physical SD, physical RTC, physical NVS,
  firmware updater, update delivery, device access, private filesystem mounting,
  merge policy, or product selection is exercised or qualified here.
