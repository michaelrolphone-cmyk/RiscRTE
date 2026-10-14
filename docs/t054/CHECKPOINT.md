# T054 persistence checkpoint — integration incomplete

T051 reconciliation: current Waveshare native/SD/System Wi-Fi campaigns pass ASan/UBSan for save, separate-process reload, edit/delete and first SD checkpoint; Watch-style production NVS/profile persistence passes normal and UBSan cold-process tests. Existing X4 native settings and Wi-Fi use namespaces 1/6; Wave selects namespace 6 only; Watch uses internal NVS. General Wave settings still need SD-backed integration. Latest factory releases are destructive; T026 preserving update qualification remains outstanding.

This opt-in Runtime repair allows a checked Fresh native baseline when SD admission/refresh is unavailable or busy. Authorization comes from the independent native inventory and Fresh receipt protocol, never from an SD error. SD vacancy is never fabricated, and first-checkpoint conflicts remain refused. Default-off profiles retain prior behavior. No product enables the flag yet.

Eight actual Runtime/maintenance/lifecycle + Wave SD/FatFs cases pass ASan/UBSan: fresh absent, fresh exported/busy, legacy absent, legacy conflict, legacy corrupt, unclassified init receipt, native error, and existing SD restore. Expected refusal cases preserve native and SD bytes. The follow-up cold-process campaign now also saves explicit user edits to both namespaces with SD unavailable, completes normal app/provider shutdown, and reloads both values and pending groups in a separate process. All ten cases pass ASan/UBSan. Returning conflicting media, namespace-1 product portability, and the preserving-update route remain integration work.

No T054 firmware is built/published and no device has been accessed.

Cold-process evidence uses the actual Wave Runtime candidate and Wave SD/FatFs adapter. The same test source is retained on the X4 Runtime candidate, but this is not a separate X4 backend run or hardware qualification. Startup-only fence instrumentation is cleared before app edits; normal final provider release is exercised.
