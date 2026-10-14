# T054 persistence checkpoint — integration incomplete

T051 reconciliation: current Waveshare native/SD/System Wi-Fi campaigns pass ASan/UBSan for save, separate-process reload, edit/delete and first SD checkpoint; Watch-style production NVS/profile persistence passes normal and UBSan cold-process tests. Existing X4 native settings and Wi-Fi use namespaces 1/6; Wave selects namespace 6 only; Watch uses internal NVS. General Wave settings still need SD-backed integration. Latest factory releases are destructive; T026 preserving update qualification remains outstanding.

This opt-in Runtime repair allows a checked Fresh native baseline when SD admission/refresh is unavailable or busy. Authorization comes from the independent native inventory and Fresh receipt protocol, never from an SD error. SD vacancy is never fabricated, and first-checkpoint conflicts remain refused. Default-off profiles retain prior behavior. No product enables the flag yet.

Eight actual Runtime/maintenance/lifecycle + Wave SD/FatFs cases pass ASan/UBSan: fresh absent, fresh exported/busy, legacy absent, legacy conflict, legacy corrupt, unclassified init receipt, native error, and existing SD restore. Expected refusal cases preserve native and SD bytes. The follow-up cold-process campaign now also saves explicit user edits to both namespaces with SD unavailable, completes normal app/provider shutdown, and reloads both values and pending groups in a separate process. All ten cases pass ASan/UBSan. Returning conflicting media, namespace-1 product portability, and the preserving-update route remain integration work.

No T054 firmware is built/published and no device has been accessed.

Cold-process evidence uses the actual Wave Runtime candidate and Wave SD/FatFs adapter. The same test source is retained on the X4 Runtime candidate, but this is not a separate X4 backend run or hardware qualification. Startup-only fence instrumentation is cleared before app edits; normal final provider release is exercised.

## Returning media and offline update planner

The Wave Runtime/actual SD-FatFs campaign now passes 12 ASan/UBSan cases. After fresh internal initialization while exported, explicit edits in both namespaces checkpoint when vacant media becomes available. A conflicting returning checkpoint remains unchanged and both edits remain pending. This is host integration, not hardware qualification; the copied X4 test has not independently passed its provider build.

`scripts/prepare_preserving_update.py` creates a fresh-readback-bound inactive-bank plan for exact ABI2/ABI3 partition layouts. It checks the caller-supplied published baseline SHA-256, confirmed active OTA selection, active pair hashes, candidate pair hashes, unchanged bootloader/partition bytes, and all protected erase ranges. It stages native/store/bank record first and emits OTA selection separately for verification and selection LAST. It never accesses a device. No layout migration, erase_flash or full-image write is permitted by this route.

`test/preserving_update_test.py` passes on exact published X4 0.1.118, Wave 0.1.35 and Watch 1.1.31 images: both active banks, all staged sector boundaries, 33 selection byte cuts, source/candidate corruption refusals, preservation of NVS/app-data/active pair, and the existing rollback selection model. Candidate store bytes in this test are synthetic, not executable firmware. This qualifies the offline plan, not ROM transport or physical update execution.

Product writer migration, a cumulative executable candidate, source-bound X4 provider qualification and final update instructions remain outstanding. No T054 firmware has been published.
