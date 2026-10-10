# Runtime 0.1.105 recovery working checkpoint

Status: incomplete reconstruction, not a release or qualified target build.

Base is the exact archived Runtime .100 source tree 676e2761c2a5292869de2bf1d78e1aadaf352617, originally recorded as commit 615fb236b591bc6974a35ae23c7b2b785c0a5016. Original Git objects and original .105 working files were lost when the execution filesystem was replaced on 2026-10-10. This commit preserves a reconstructed source candidate; it does not assert byte identity with the lost .105 source or firmware.

Focused changes: selectable requirement rows 17 and policy rows 18. Defaults remain 16/16. Existing deployed 16/17 is preserved. Other Runtime capacities remain unchanged. No .103 global-capacity or .104 KV feature is imported.

Fresh checks: seven header acceptance/rejection compile cases pass. Actual Runtime.cpp optimized object bytes match the archived .100 source for both 16/16 and 16/17 when compiler source-path differences are normalized. Selected 17/18 production admission, full graph tests and target build still need fresh qualification.

Target build must set PLATFORMIO_SETTING_ENABLE_TELEMETRY=No. Installation requires a native-first preserving upgrade from .53/.100 with the old store before Home gains its seventeenth requirement and eighteenth policy row. No blank AppData/NVS, full-flash reset, updater transaction or hardware validation is claimed.

The GitHub parent is a transport anchor only; this working tree is the restored .100 branch plus this narrow successor, not a claim of integration with current main.
