# Runtime 0.2.5: HCI lifecycle alongside owned Wi-Fi

This increment starts from Runtime 0.2.4, commit
`32bf33b9e7b29da1f084b81431b63c7ea91da391`. It preserves the explicit 29-provider /
45-grant PSRAM cohort option, the ordinary 26/42 and legacy 17/32 capacities,
TCP, entropy, IQ ownership, all existing public layouts, and the HCI transport.
The live branch/tag/open-PR check for the local 0.2.5 assignment is recorded in
`evidence/hci-wifi-coexistence/version-reservation.json`. Publication and product
selection are separate owner actions.

## Lifecycle contract

The previous CpuPort rejected HCI open and close whenever a native async radio
operation existed, including a healthy connected Wi-Fi session. Consequently,
BLE setup could not be enabled while Wi-Fi WebDAV remained owned.

The new guard acquires `Hardware.radioAsync.tryShared` before checking phase
readiness and holds it through the entire controller lifecycle, any failed-open
rollback, the final native idle proof, and the token update. Every path releases
exactly the acquired level through `endShared`. It never keeps a shared lease
for the lifetime of the BLE session and never waits for the Wi-Fi worker.

NativeHardware continues to use its existing `NativeRadioResourceOwner` adapter
over the worker's atomic lease. Its table now routes `sharedReady` through the
same owner's `ready` method. An existing Files service lease therefore permits
a nested HCI operation without falsely treating its own lease as contention.
Readiness is checked while leased, including for nested calls: a queued operation
or cancellation that occurred inside an outer service still rejects HCI.

| Native radio condition | HCI open/close admission |
| --- | --- |
| Radio idle, no worker SDK iteration | Allowed |
| SCANNING, JOINING, CONNECTED, RESULTS, between SDK iterations | Allowed |
| Worker executing any SDK iteration, even a connected-state query | Deferred |
| QUEUED or STARTING | Deferred |
| Cancellation requested or STOPPING | Deferred |
| CLEANUP_FAILED or CpuPort radio closing | Rejected with existing custody retained |

All four admitted operation phases have completed Wi-Fi initialization. They
allow the SDK's normal Wi-Fi/BLE coexistence; they do not imply RF inactivity,
a healthy network peer, or physical qualification. CONNECTED is the intended
WebDAV case, while SCANNING/JOINING/RESULTS use the existing shared-resource
contract consistently. Admission never polls the worker or substitutes a copied
phase snapshot for the atomic exclusion lease.

The bool HCI API still has no distinct BUSY result. A contention refusal starts
no HCI SDK call: open returns zero token and close preserves the exact existing
token and closing flag. Callers may retry after the worker settles. A real
partial activation or failed disable/deinit retains the cleanup token and
existing lifecycle fences; matching owner cleanup can retry. An uncertain
controller initialization remains retained, even if SDK status reports IDLE.
Wrong owners, wrong tokens, IQ ownership, sleep/retained state, overflow, RX/TX
bounds, and retained dependency custody keep their existing checks. Legacy
Hardware without the async table keeps the earlier radio-idle fallback.

## Qualification

`test/run_hci_radio_coexistence_test.sh` links actual CpuPort, NativeHci,
NativeRadioAsync, and NativeRadioResourceOwner against deterministic SDK
fixtures. It does not replace the production lease with a fixture lock.

Its 23 scenarios cover all admitted phases, queued/nested cancellation refusal,
Files-style nesting, both HCI open and close versus a gated Wi-Fi SDK worker,
HCI allocation/init/enable/callback/disable/deinit boundaries, failed-open
rollback failures, cleanup retries, permanent uncertain init, wrong owner and
token, IQ exclusion, token exhaustion, malformed native table, and 100 repeated
HCI open/close sessions against a continuously running native Wi-Fi worker.
Every HCI allocation, SDK lifecycle boundary, status check and free in the
single-step cases tries to advance the real worker while the owner holds its
lease, verifying no Wi-Fi SDK call can enter. The concurrent case runs under
ThreadSanitizer. Existing HCI lifecycle, IQ, and async production Wi-Fi provider
regressions are also run.

Commands:

```sh
bash test/run_hci_radio_coexistence_test.sh
SANITIZE=1 bash test/run_hci_radio_coexistence_test.sh
TSAN=1 bash test/run_hci_radio_coexistence_test.sh
STAGE_LOGS=0 bash test/run_hci_radio_coexistence_test.sh
bash test/run_hci_test.sh
bash test/run_radio_iq_test.sh
bash test/run_radio_async_test.sh
```

The traced local executor requires `ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan
execute, while LeakSanitizer cannot execute under ptrace. The tests explicitly
check clean HCI allocation/free balance and retained allocations remain owned.

The target is built single-job from the committed source checkpoint using the
same pinned framework/toolchain and previously qualified local esptool 4.11.0
substitution used for 0.2.4. Both 26-provider and capacity29 IQ/TCP/entropy
variants retain the mandatory 64 KiB IQ bank and alias-exclusion post-link
proof. The linked capacity proof checks the exact firmware and ELF markers.
Final build receipts identify the source checkpoint and binary hashes; no
earlier binary is relabelled as a new source revision.

Host tests and target builds establish source-level lifecycle and static-link
properties. They do not establish simultaneous RF performance, vendor SDK
latency on a physical watch, dynamic heap/stack high water, successful WebDAV
requests during BLE traffic, or physical power behavior. No device was touched.
