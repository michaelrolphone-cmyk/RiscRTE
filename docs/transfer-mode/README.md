# Dedicated serial transfer mode candidate

Explicit product opt-in: `RISC_NATIVE_SERIAL_TRANSFER_MODE=1`, paired banks and serial file service. Existing successful HELLO wire layout is unchanged. A validated fresh HELLO requests cooperative cancellation only on an already-confirmed committed pair with idle update custody. Pending trials return BUSY before SID commitment and keep normal Home health/rollback behavior.

HELLO success waits for checked app/fini/native-settings cleanup, exact native-file provider keep-set parking and the actual full-ready owner checkpoint. Catalog readiness does not mean every namespace is mounted. Healthy accepted Settings work drains; new background discovery/maintenance and unrelated graph polling stop after intent. No already-running provider callback is suspended or assumed completed.

Mode intent stays latched until manual reset. Genuine disconnect and normal 30-second session/operation expiry discard protocol state but do not restart apps. Existing NS keepalive supports long file selection; expired sessions require a fresh HELLO. Duplicate HELLO replays historical admission success, not current media availability. No automatic reset operation is added.

The selected Home must be rebuilt with `--serial-transfer-mode` (default role only). Existing .63 retained-Home packaging cannot implement the new cooperative exit boundaries. Existing ABI `exit_requested` is used; no SDK layout change. Other products remain feature-off.

## Qualification and limits

The frozen baseline witnesses two live-stack HELLO successes against the newly selected parked-before-HELLO contract; it does not prove legacy protocol noncompliance or physical cause. Current actual Runtime/Files host suite passes30 cases normally and with ASan/UBSan (LeakSanitizer disabled due ptrace). A pre-fix source-variant control reproduces deadline BUSY erasure. Actual NativeSettingsLifecycle/CpuPort fixture drains accepted work and prevents new maintenance. NativeBankStore/PairedBank host tests cover confirmed/pending/torn metadata and preserve rollback. Actual shared native parked loop, Console and SleepDiagnostics service71-byte PATH through repeated64-byte polls under blocked/partial TX and disconnect.

Boot eligibility in scheduler/USB fixtures and SDK hardware are modeled; actual native bank is tested separately. No device execution, hardware timing, firmware link, release readiness or browser automatic-reset integration is claimed. Separate actual namespace2 LittleFS/namespace12 FatFs composition passes8 cases normally and sanitized, preserving namespace2 usability with absent SD. Full target Runtime build, successor producer/version integration and physical qualification remain open. Current Settings/USB/Bank receipts use the final unchanged source; older settings-qualification.json is retained as historical evidence only. The bank-admission negative control restores only the rejected operationSafe condition and fails the expected real-adapter assertion.

No PR readiness transition or merge is requested. This branch is rooted at published Runtime0aab2fd3, currently two commits ahead of main; it must not bypass pending PR98 policy.
