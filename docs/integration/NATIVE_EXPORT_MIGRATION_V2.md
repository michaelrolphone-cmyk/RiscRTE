# Native export-map migration V2

This focused change builds on provisioning Runtime 498e642 and peer TCP 1a1bd646. The combined Runtime version is 0.2.14. System peer frontend is 56fdeabe, BLE setup is b2419513; X4 provisioning source 70bd67a is merged separately. No capacity or software-service selector changes are part of this patch.

`validateNativeCohort` accepts a separate V2 native policy. Select `RISC_NATIVE_APP_DATA_EXPORT_POLICY_V2_HEADER` at native build time; the header returns `selectedNativeAppDataExportPolicyV2()`. The linked `risc_native_app_data_export_policy_v2_record` is inspectable. V1 layout and default immutable-map admission remain unchanged.

V2 metadata contains schema version 2, migration ID, exact source and target product/version/repository/revision/Runtime-version coordinates, and complete before/after `app_capabilities` JSON snapshots. The snapshots enumerate the export maps and operational grants. They are immutable build inputs, never authority supplied by an app, provider, boot file, or download. A mismatch fails admission before any ELF is admitted. For unrelated future coordinates the record is inert and ordinary preservation checks apply.

An explicit shared-KV transition lists the recipient, API, namespace and exact previous app-owner set. All entries must be consumed exactly once. Missing, duplicate, unrelated or extra owner records reject. Existing persistent app namespaces, provider keys/files, installation namespace range and consumer grants remain subject to their ordinary preservation checks. Existing data is never copied, rewritten, formatted or renamed by migration admission.

Native-first composition is required when the installed native image predates V2: first admit a bridge with unchanged authority and this native policy; then admit the exact target peer cohort. The paired-bank transaction remains the only durable selection mechanism. An interruption before selection leaves the old bank authoritative; pending-target boot/confirmation and old-bank rollback retain the existing transaction behavior. Rolling back selects the untouched old bank; it does not apply a reverse data migration.

Qualification includes old one-file map to expanded map, first-map plus operational KV grant, absent authority, schema and source/target mismatch, altered maps, wrong/missing KV owners, repeated admission, unchanged installed target, ordinary future unchanged maps, and old-bank re-preparation. These are inert host admission tests; they do not establish device radio or flash behavior.
