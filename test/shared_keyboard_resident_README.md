# Host resident shared-keyboard capacity fixture

From the Runtime repository root, the final X4 paper source selection is explicit:

```sh
python3 test/run_shared_keyboard_resident.py \
  --output /workspace/shared/shared-keyboard-resident-final-normal \
  --ble-root /workspace/shared/x4-ble-shared-text-preserve
python3 test/run_shared_keyboard_resident.py \
  --output /workspace/shared/shared-keyboard-resident-final-sanitized \
  --ble-root /workspace/shared/x4-ble-shared-text-preserve --sanitize
```

Run with `--activation demand-retained` to exercise the product activation policy.
The default `demand` policy deliberately forces all 26 selected providers to be
pinned at Home promotion, conservatively stressing total grant occupancy.
`--input touch` (default) drives real onscreen scene keys without declaring a
hardware-keyboard dependency. Supplemental `--input hardware` injects synthetic
USB HID events and adds the keyboard declaration to the text provider; it does
not qualify physical keyboard support. Each invocation independently exercises
both clients in accepted, cancelled, Back, pending-close, retained-close, and
handoff modes. Use `--client`, `--modes`, and `--help` for focused/configurable runs.
The default BLE source is the earlier experimental tree; always pass the restored
paper source above for the X4 capacity checkpoint. The earlier source's actual
240x240 refusal can be checked separately:

```sh
python3 test/run_shared_keyboard_resident.py \
  --output /workspace/shared/shared-keyboard-resident-original-ble-gate \
  --ble-root /workspace/shared/ble-shared-text-input \
  --client ble --modes entrypoint
```

`entrypoint` is rejected for a selected paper-capable BLE source. The paper BLE
app's full entrypoint/renderer behavior requires its separate app-level fixture.

## Scope and interpretation

These are real Home startup and resident transition functions, actual Points/BLE
naming and cleanup functions, production adapters, scene/text/profile providers,
and production Runtime/ProviderGraph. Wrapper entrypoints script the control
paths, not complete interactive application loops. Home's live grants remain
owned during the foreground modal. Product app capability policy, namespaces,
board instance IDs, native-time/tagged-alarm/resident flags, and installed client
feature flags come from the selected receipts and manifests.

Peripheral service tables, alarm/RF/storage behavior, and nine provider-padding
nodes are test doubles. Unused physical device bindings are removed; this is not
a complete production hardware dependency graph. Handoff reaches a zero-grant
synthetic launcher. Points uses an empty/default catalog and edits a type draft;
BLE uses one copied completed device. RF, full-catalog/full-scan workload peaks,
Home menu interactions, physical input, and target execution are not covered.

Each `PHASE` row has exact live graph/boot/host/foreground attribution; invocation
counts before/after the slash mean all grants/provider-backed grants. `graphPeak`
is exact at every successful graph acquisition. Reported invocation peaks are
sampled maxima and may miss intermediate states. Requested-live bytes/blocks and
allocation/free calls cover wrapped app/provider allocations, not host Runtime
metadata, loader/linker, static/BSS/stack memory, static fake panel storage,
allocator bookkeeping/rounding, or transient realloc copies. These host64 bytes
are workload observations, not target heap/PSRAM sizing.

All clean modes assert zero final graph grants and tracked allocations. Retained
close deliberately injects failed touch unsubscribe, keeps custody and requested
bytes, and verifies a repeated Runtime run does no allocation/free/provider I/O.
The runner's ASan leak check is disabled for that deliberate retained lifetime;
clean-path tracked-allocation emptiness is still explicitly asserted.

`evidence.json` records every independent run, policy/input/sanitizer settings,
limitations and log hashes. `build-inputs.json` records actual source commits and
dirty state, tree inventories, all compiler-reported source/header dependencies,
SHA-256s, exact commands/flags, and artifact/toolchain fingerprints. A dirty source
revision must be interpreted with its content hashes. `--skip-build` verifies
saved inputs and artifacts before reuse; moving a build does not change the
original compiled absolute-path provenance.
