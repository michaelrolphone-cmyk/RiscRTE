# Final unique-stage synchronization qualification

Implementation is writer 7dd80141a25f874d965d47747a7123c534d451bd with catalog/session
6200421bb49c27d94c6f815239281298b48bc69f and removal of the unused private Recovery
stage-deletion authority 140a57ed3c8289a23ee67e6c3ff0ad3036e01894 (local c6d91b1).
The source manifests capture the exact Runtime/fixture/header inputs and external
X4/Drivers/Reader pins for final actual normal, ASan/UBSan and Port runs.

## Passing results

- All 31 actual Runtime/Port/X4/FatFs/coordinator cases pass normal and ASan/UBSan.
  Includes two checkpoints, continuously polling native task, real app BUSY ->
  release -> next accepted edit, copied-work cancellation/committed receipts,
  clean-unavailable defer, foreign/changed/divergent/legacy conflicts, removal
  and checked-close retention, exact selected-marker recovery, source-mismatch
  refusal, interrupted A -> reload -> actual B edit -> publication -> reload/ack,
  and exact preservation of A stage plus both A/B page sets.
- Actual target-page empty/prefix repair and nonprefix/oversize preservation;
  deterministic owner polls assert zero native I/O and zero sector I/O.
- Ordinary Files-style open/read/close works before/after with unchanged unrelated
  bytes and safely refuses during custody; USB refusal/resumption is separate.
  The actual Files application and physical USB host are not executed.
- All 16 original actual Port/custody cases pass.
- Lower page/stage collision tests pass normal and ASan/UBSan: four deliberately
  sparse directory APIs refuse before native/SD I/O or marker creation; ten
  collisions prove exact full byte reuse without a remove call, empty/prefix
  repair, and nonprefix/oversize preservation. Legacy 360 handoffs / 436 I/O cuts pass.
- Final durable export suite passes normal and ASan/UBSan: 684 SD returned-error/
  power cuts, 165 cancellation/lease/restart handoffs, 164 recovery failures
  (10 retained), 412 recovery handoffs, 1,122 native marker-arm/retire/ack failures
  (24 retained, 72 uncertain), plus prefix/later-edit and format-capacity checks.
- Retained-export, Core, export-marker, coordinator, native worker-binding and
  ordinary catalog regressions pass. Catalog preserves original behavior across
  1,016 handoffs, 604 I/O cuts and 504 vacancy/obstruction cases.
- Actual Xtensa legacy/cohort header compile passes; permanent Sync control block
  is 15,992 bytes, excluding dynamic map/inventory allocations.

ASan runs intentionally use detect_leaks=0 because sticky-retained fixtures keep
live permanent objects and end through _Exit. No leak-freedom claim is made.
Power-cut/restart tests use durable native/SD models or reconstruction of the
checked native store/core; they do not simulate a physical board reset.

## Scope

Catalog admission is read-only. Marker recovery adds no stage/page cleanup; its
existing reader can invoke the provider's checked rollback recovery if the index
is absent. The original exact rollback protocol can rename/remove its own journal;
this is distinct from arbitrary staging cleanup. The new writer never removes
unrelated canonical pages or stages, and only repairs colliding current-target
prefixes proved by its captured marker/image and checked metadata/content.

Production startup/private native-store provisioning and admission, permanent
worker/object/control-memory registration, qualified first incarnation, and the
trusted owner begin/poll plus validated Port-custody hook are still separate.
Nothing is activated in production; no private mount, Watch change, device action,
firmware delivery or GitHub publication is included.
