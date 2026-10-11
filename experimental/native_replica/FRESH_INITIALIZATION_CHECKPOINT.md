# FreshEmpty initializer recovery: experimental working checkpoint

Implementation base: published `d3f0e7b8450d7f0e5b59c6bbe4cd34ee89e5edbd`.
This isolated source is disconnected from product, SDK and production storage.

The purpose-bound RTENFI1 receipt occupies the existing 48-byte root blob. Core
loads it as `Initializing`, not Missing. Only the exact one-shot Fresh admission
may reserve, seal the canonical 484-byte RTENR03 empty image, and replace that
receipt. A retry uses a new monotonic object ID. Old RTENINIT remains unclassified.
Store revalidates exact source witness, live owner/lease and owned artifacts
before reservation, seal and root replacement. The final native root is selected
atomically, without deleting the receipt first. No SD or entropy service is used.

## Current test status

The dedicated actual checked AppData/NVS host runner passes:

- no-SD/no-entropy create, initializer reentry refusal and later edit preservation;
- every 0..508-byte exact owned `.pending` prefix (509 cases);
- 17 malformed/foreign/source/counter/alternate-entry refusals;
- 624 before/after create/resume fault cases: 86 Retained, 156 Uncertain;
- selected root or exact receipt recovery after simulated reboot, with no defaults
  publication over uncertain evidence and no source cleanup.

Reproduction: `bash test/run_native_fresh_initialization_test.sh`.
Sanitizer/full-chain adaptation and strict Xtensa compilation are still pending
at this working checkpoint. Source was reconstructed after an executor reset;
Core/factory hashes match their pre-reset files exactly, and the reported checks
above were rerun on the reconstructed source.

The source collectors in `test/native_fresh_source_fixture.h` are synthetic host
inventory adapters, not a concrete production inventory implementation. Legacy
rows/journals are explicit test facts. Non-reentrant owner callbacks and the
exclusive native serialization lease remain mandatory.

The read-only private-artifact inspector qualifies Linux nonalias checks only.
The ESP path currently fails Io before filesystem I/O: hardware/VFS nonalias and
checked-close capability qualification remains an activation prerequisite.
No host test or target compile establishes power-loss atomicity on hardware.

## Storage and layout

Persistent overhead: no new key, no root size increase, no image/header increase.
Receipt 48 bytes; canonical empty image 484 bytes; object wrapper plus image 508.
Host sizeof: Core 280, Initialization 336, receipt handle 56, admission factory184,
checked store256, inspector224. Allocation deltas and target layouts remain to be
measured in the completed qualification checkpoint.
