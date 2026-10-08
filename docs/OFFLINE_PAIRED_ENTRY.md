# Explicit owner-controlled offline paired entry

`scripts/paired_entry_plan.py` prepares private files and compares frozen
snapshots. It never opens a serial port, reads a device, flashes, resets, erases
or changes a live selector. It is a separate entry mechanism for a device whose
ordinary update client cannot obtain its first qualified update. Ordinary native
OTA admission, policy-row limits and confirmation remain unchanged.

This first implementation accepts only the exact 16 MiB ABI2/app-data layout and
the existing pinned QIO rollback bootloader. A different bootloader, including a
DIO experiment, is refused. It does not create a migration from another layout.

## Required trusted product wrapper

The Python API is:

```python
plan(inventory_path, original_snapshot, firmware_path, store_path, output, admission)
verify_phase(inventory_path, original_snapshot, firmware_path, store_path,
             output, current_snapshot, phase, admission)
```

`admission(old_firmware, old_store, new_firmware, new_store)` is an explicit
callable supplied by trusted product code. No JSON field selects code, imports a
module or asserts admission. The generic CLI refuses unvalidated invocation.

The wrapper independently checks its committed product/Runtime source receipts,
both native candidates and ELF/proof bytes, the source native's own installed
store, the receiving native's old/new namespace comparison and the receiving
native's complete new store. A receiving 17-row parser can perform the explicit
offline comparison; that does not permit an installed 16-row native OTA parser
to admit the same target. Board identity and any namespace migration must match
the separately qualified product route.

Return a bounded JSON object containing `schema`, integer `schema_version: 1`,
`layout`, `source_firmware_sha256`, `source_store_sha256`,
`target_firmware_sha256`, `target_store_sha256`, and an `admissions` object.
The latter contains exactly three nonempty receipt objects: `source_self`,
`receiving_transition`, and `target_self`.
The generic layer compares all four hashes to its frozen byte inputs. Additional
source, cohort and policy proof fields are preserved; include the committed
`validator_source`. The entire receipt is limited to 256 KiB. The callback gets
the exact full SPIFFS partition bytes. It may omit only a valid 32-byte legacy
`.provision-sha256` entry from graph/file-map comparison, without changing the
raw source digest.

## Private owner inputs

The inventory is an exact JSON object with:

- `schema: "riscrte.offline-paired-inventory"`, `schema_version: 1`
- `layout: "riscrte-paired-appdata-v2"`, `flash_bytes: 16777216`
- `active_bank: 0` or `1`
- `running_firmware_sha256`: the independently known source native digest
- `quiescent: true`: the owner's explicit live-state precondition

The owner supplies an original full flash snapshot obtained in deliberately
selected ROM mode. The planner validates the partition table, bootloader,
confirmed OTA selection, both committed journals and their firmware/store
contents. Unconfirmed, unknown, corrupt or uncommitted nonblank state refuses.

Snapshots may contain private credentials and personal data. All output is
private, created outside Git/publication directories with owner-only access.
The planner reads NVS/app-data solely as snapshot bytes for preservation hashes;
it neither decodes their contents nor includes them in a write/restore payload.
Do not publish real snapshots, backup payloads, plans or their private hashes.

## Write and readback boundaries

The output supplies five ordered writes, each followed by a fresh snapshot and
the matching `verify_phase` call before continuing:

1. `invalidate-journal.bin`: erase destination readiness; phase `invalidated`
2. `enter-application.bin`: exact new native plus erased slot padding;
   phase `firmware-staged`
3. `enter-store.bin`: exact admitted complete store; phase `store-staged`
4. `enter-journal.bin`: new paired record and inherited consumed-profile
   receipt; phase `journal-staged`
5. `enter-otadata.bin`: one alternate OTA page with state NEW; phase `pending`

Begin with phase `ready`. Only the inactive native/store, its journal sector and
the alternate OTA sector appear in the write map. Everything else, including
the original active pair, partition table, bootloader, NVS, app-data and unused
flash regions, is compared to the original snapshot. Torn or out-of-order bytes
refuse instead of guessing how to continue. The tool never formats a filesystem.

The active consumed-profile receipt takes precedence over the legacy digest
file, exactly as in `NativeBankStore::sourceReceipt`. The new receipt binds that
same profile digest to the new pair and the actual current source pair. This
prevents preserved owner inputs from unexpectedly reconsidering an old profile
after the offline update. Record/receipt serialization is checked against the
production C++ implementation.

The generated selector is never VALID. The receiving native and healthy default
application must perform their ordinary confirmation. Phase `confirmed` accepts
only the corresponding observed VALID page; phase `rolled-back` accepts only
the corresponding ABORTED target page with the original confirmed page intact.
These are snapshot checks, not evidence of physical execution. Saved backup
files are recovery evidence; they do not authorize automatic restoration or
overwriting running code. Never restore NVS/app-data to make a comparison pass.

Normal applications may change their own saved state after boot. A later hop
must use a fresh confirmed snapshot, independently known native identity and its
own product admission. Do not reuse the first snapshot or pretend confirmation
happened. In particular, the qualified X4 `.20 -> .25` and `.25 -> .26` routes
remain two separate plans with two normal confirmation/rollback boundaries; the
second migration must not be silently retargeted to `.20`.

## Verification limits

`python test/paired_entry_plan_test.py -v` tests geometry, both bank directions,
readiness invalidation, ordering, pending/confirmed/rollback states, corruption,
private output custody and receipt inheritance. Its admission callback is an
explicit synthetic transaction fixture. Real product qualification must also
execute the trusted adapter against the exact route artifacts and snapshots.

Host proofs do not establish live ROM mode, electrical behavior, flash power-loss
recovery or physical device health. Hardware remains unrun. The owner must
independently control and verify every physical step.
