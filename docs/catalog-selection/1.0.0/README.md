# Existing-card file selection 1.0.0

Choose individual files from a validated catalog payload while preserving other
files and app data. The selection document records the current destination bytes,
the user's create/replace choices and the immutable payload identity. The reference
validator and state machine provide deterministic conformance inputs for the
catalog's existing installer.

## Interface and compatibility

The base remains `catalog-payload/1.0.0`. Its manifest, container, descriptor,
firmware identity, source inventory and namespace bindings retain their exact
bytes and hashes. Existing clients continue to execute each original manifest
operation and its declared `previous` guards.

This is a separately selected execution mode:
`mode: explicit-current-file-selection`, `selection_version: 1.0.0`.
The catalog must recognize it explicitly; it must never pass these decisions to a
legacy executor by editing the downloaded manifest or re-sealing its trust pins.
No release descriptor or frozen manifest field needs changing.

`replace-observed` authorizes replacement of the current file shown to the user,
including a customized file that differs from a legacy predecessor. Its guard is
the newly observed full size/SHA-256. The report preserves the original operation
and predecessor, and labels differences or ownership that RTEF1 cannot establish.
It does not say the legacy guarded operation passed. The user may select the new
replacement after seeing its current identity and destructive effect; a publisher
need not republish the payload for that user's existing bytes.

The base inventory determines eligible source files and destinations. Every row
is accounted for, with one explicit decision:

- `create`: the admitted destination returned authoritative STAT NOT_FOUND.
- `replace-observed`: the current regular file was fully read and the user approved
  replacing that specific size/SHA-256. `consent` is true only for this choice.
- `verify-equal`: current full size/SHA-256 equals the payload. Verify it without
  writing or truncating it.
- `skip`: leave that file untouched, including a declined replacement.

The UI owns consent. A JSON boolean or a digest cannot authenticate a human
choice. Defaults and inferred conflicts cannot generate replacement consent.
Once a choice is recorded, changing its destination, current bytes, source pins
or selected operation requires a new review and decision.

Selected-file installation and full-payload readiness are distinct. A selected
subset can finish while other files are skipped. The receipt reports both
`selected_files_verified` and
`all_inventory_bytes_verified_after_final_firmware`. It never substitutes subset
success for the base manifest's declared combined-update completion policy.
The catalog applies its actual firmware/product requirements and establishes
consumer usability. Mere membership in an inventory does not force installation
of every app when the user selected a subset.

## Inputs

- `observation.schema.json`: exact manifest/container/inventory and trusted-context
  hashes; the proven running firmware ID; the existing RTEF1 session number;
  actual namespace IDs/writability; every destination and declared parent.
- `selection.schema.json`: that unchanged binding, the observation digest, and
  an ordered decision for every immutable inventory row.
- The existing trusted context supplies product/profile facts, generic namespace
  bindings, exact source and install pins, and provider capabilities. Namespace
  IDs stay decimal strings and paths retain exact UTF-8 bytes.

Only STAT status 1 for a previously admitted namespace becomes `absent`.
UNAVAILABLE (-3), BUSY (-4), IO (-2), RETAINED (-6), sequence disagreement (-10),
unknown statuses and uncertain outcomes remain distinguishable failures.
Namespace enumeration alone does not prove provider availability. A namespace
label, backend name or physical mount path never routes a file.

A regular-file identity requires successful GET, complete READ, CLOSE/count/CRC32
and host SHA-256, plus size/type recheck. Empty files also require GET/CLOSE.
The read adapter uses the existing `scripts/serial_files.py` client and accepts an
already-owned live client. Firmware identity comes from the catalog's product flow;
RTEF1 has no firmware-identity or card-UUID command.

The same live owner object and RTEF1 session scope each execution. Serialized
observations/selections are review and audit data, not resumable write authority.
After reconnect, restart, detected media removal or firmware change, establish the
new product/session/admission facts and gather new observations. No new mandatory
device UUID, publisher approval service or signing system is introduced.

## Immutable inventory versus selected capabilities

The frozen validator combines immutable inventory checks with whole-plan provider
limits. The selection adapter uses a private structural-validation view to reuse
those checks while deferring provider execution limits to the selected rows.
That temporary view is never an admission result or an output. The actual trusted
context is preserved and hash-bound throughout.

All original asset hashes and lengths, canonical ZIP bytes, schema, source and
parent inventories, path safety, aliases, product compatibility, bindings,
consumer evidence, firmware identity, install effects and ordering checks remain.
Selected actions are then checked against the actual provider's operations,
writability, hierarchy, path/component/file bounds, naming restrictions and sum
of selected write bytes. A read-only equal-file selection can pass; a new
replacement still requires an actually admitted writable replacement capability.
A small subset does not consume the entire manifest's write budget.

## Builder procedure

1. Validate the immutable container and trusted context. Show eligible app/file
   choices, their exact destinations and the target firmware association.
2. Hold the existing exclusive port owner. Confirm running firmware and actual
   namespace admission. Observe destinations and parents. Gather full identities
   for regular files; errors and uncertain observations are not absence.
3. Show original operation/guards, current and replacement sizes/hashes, and the
   destructive, non-atomic effect of `replace-observed`. Record each choice.
4. Revalidate all pending selected destinations and required parents before
   mutation. Prepare only parents needed by selected writes, ancestor first.
   Before each MKDIR, recheck that exact parent: an existing directory passes;
   only authoritative NOT_FOUND permits MKDIR; an existing file or other status
   fails. Do not create parents used only by skipped files.
5. Immediately before each file's PUT, re-read its full current identity and
   verify the same guard, owner, session, firmware and namespace. Confirm parents
   are directories. A changed guard stops this selection; do not auto-convert a
   create to a replacement or reuse an earlier overwrite choice.
6. Consume the one-shot instruction using the existing RTEF1 create/replace
   operation. Transfer only the pinned source bytes. Record acknowledged progress,
   successful COMMIT and complete SHA-256 readback. Equal-file choices issue no PUT.
7. Apply the existing supported firmware sequencing. After its last firmware step,
   collect a new final observation and verify the needed target files and consumer
   usability. Keep selected-file completion separate from complete associated
   payload readiness.

The reference's `SelectionRun.recheck` returns a single-use file instruction.
`progress` accepts acknowledged bytes; `finish` records verified, definitive failed
or uncertain results. `invalidate` closes the selection on cancellation, media
loss, restart or disconnect. The builder retains protocol-level error handling:
known-handle cleanup follows the existing client; an unusable session sends no
cleanup or automatic retry/reconnect/reset. Losing PUT acknowledgment may mean
truncation; losing COMMIT acknowledgment may mean completion. An explicit new
inspection and selection is needed for recovery.

Only selected individual files and their necessary parents may be changed.
Replacement is destructive at PUT and provides no atomic compare-and-swap,
rollback, rename or power-loss durability guarantee. Hold exclusive access during
observation and mutation. RTEF1 cannot detect every same-size concurrent edit or
an undetected card swap; it supplies neither a physical media identity nor a hash
predicate in PUT. Known changes and uncertain results stop the operation.

## Conformance

Python 3.10+ and the existing pinned payload dependency are sufficient:

```sh
python3 -m pip install -r docs/catalog-payload/1.0.0/requirements.txt
bash test/run_catalog_selection_test.sh
```

The runner checks both unchanged payload conformance and this selection contract.
`make_schemas.py` and `make_examples.py` generate exact deterministic schema/vector
bytes. Six committed vectors cover clean, mixed, all-existing, all-equal,
selected-subset and customized-legacy inputs. Source-authored tests also cover the
45-file three-replacement/42-create shape, source/binding/guard tampering, provider
limits, cancellation, removal, restart, partial acknowledged progress, lost ACKs,
parent conflicts and preservation of sibling files, app data and other namespaces.

Protocol-adapter tests use the production Python client with exact request/reply
transcripts for missing/unavailable/error distinctions, legal short reads, empty
files, complete CRC-checked CLOSE and namespace admission. The in-memory mutation
fixture applies only the returned individual-file instructions.

The external catalog builder owns UI, consent events, serial/flash orchestration
and browser integration. These contract fixtures establish software behavior;
physical-device and deployed-browser qualification remain separate evidence.
