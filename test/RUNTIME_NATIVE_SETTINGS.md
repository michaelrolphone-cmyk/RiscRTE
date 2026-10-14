# Runtime broker and native settings worker roundtrip

`run_runtime_native_settings_test.sh` closes the gap between the separate
Runtime-broker/System-controller and native-coordinator/checked-store fixtures.
It composes these production implementations in one host executable:

- Runtime dynamic application invocation, exact atomic namespace-6 grant,
  revocation and app/provider lifecycle barriers
- NativeSettingsCoordinator, NativeAtomicKeyValue and the compact native
  maintenance mailbox, with a single permanently registered reload client
- CheckedNativeReplicaStore, AppDataFiles and NvsReplicaMetadata
- The actual System Wi-Fi controller, atomic profile adapter and profile codec
  pinned to `a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89`
- App KV1/KV2 and actual dynamically loaded provider bound-KV1/bound-KV2 tables,
  routed through Runtime's native namespace-6 read-through

The host worker continuously calls the production coordinator `workerStep()`.
It is not created per request, and the owner never dispatches a storage step.
The same coordinator gate arbitrates the atomic endpoint and trusted native
reloads; host owner/worker identity and background-resource hooks enforce one
held lease. Deterministic pauses affect only host scheduling. The in-flight
pause occurs inside an actual wrapped AppDataFiles write.

ESP-IDF NVS calls, partition-space accounting, platform task identity and
background-resource hooks remain host stand-ins. UI/radio providers are the
existing System synthetic fixtures. No network, device or real credentials are
used. This does not execute the FreeRTOS task binding on hardware.

## Explicit precondition and limits

The fixture first writes a synthetic pre-established empty native domain using
compatibility initialization. Its origin is asserted to remain `Unclassified`.
That seed fixture is destroyed before the first Runtime is created. This is
only test-media setup, not proof that first boot, legacy import, SD-only restore
or any unknown/absent source is admissible. No source-admission token is forged,
no source gate is replaced, and no production first-boot path is selected.

Every Runtime starts with a new checked store, Core and coordinator. A registered
native mailbox reloads the existing NVS-selected filesystem object through the
actual checked stack before Runtime admission. The second boot shares only the
host filesystem and simulated NVS media with the first; the first Runtime,
coordinator, store and Core have all been destroyed after joining their worker.

This is not SD transport/admission, NVS hardware durability, physical power-cut,
private-mount provisioning, target stack/latency, LittleFS, Watch policy or
whole-product qualification. No production enablement or publication is made.

## Cases

`roundtrip` proves:

1. The actual controller's Save submits a copied mutation group and semantic
   intent. The app adapter wipes staging, the visible draft changes before the
   worker runs, and the original profile and original intent are durable.
2. Preparing a connection performs no storage I/O. Only synthetic successful
   connection enables Remember. While its actual file write is blocked, Back
   cancellation cannot discard a committed result or release native custody.
3. A third queued Save is cancelled before dispatch. It produces no filesystem
   or NVS I/O, no operation ID and no third saved profile or pending intent.
4. Accepted atomic work cannot release its Runtime capability, release the
   background lease, admit competing native work or pass resident reset safety.
   One thousand owner polls per pending checkpoint perform no native I/O.
5. App/provider KV1 decodes both profiles and KV2 returns byte-exact values.
   The production codec's KV1-only version check is preserved, not bypassed.
   Legacy backend reads/writes cannot run, legacy writes remain denied even
   with a read-write map, and busy/stale/foreign/unmapped reads fail correctly.
6. Two profiles and both `WFI0001` semantic intents survive the new checked
   store/Core/coordinator/Runtime reload. The actual saved-network list and
   profile selection show the restored values.
7. Each invocation runs 20,000 state/get pairs while the permanent worker polls
   idly, with no filesystem/NVS I/O, spurious Retained state or leaked lease.
   Successful completion clears native copied scratch and stale app tables.

`pending-exit` returns from the actual dynamic app with an uncollected queued
Save. Runtime retains the app/providers, suppresses fini and provider stops,
invalidates exposed tables, and keeps the native/background lease. No storage
I/O occurs after the queued owner checkpoint.

`retained` injects one actual checked close failure. The controller keeps its
pending token and refuses suspend; the checked store and coordinator remain
Retained. Runtime retains app/providers and both lease barriers, suppresses
cleanup callbacks and rejects stale tables. Both retained cases join the host
worker, then exit the process without destructing retained objects or retrying
cleanup.

## Reproduction and evidence

Run from Runtime:

```sh
export SYSTEM_ROOT=/path/to/pinned/System
EVIDENCE_DIR=/tmp/native-settings-plain bash test/run_runtime_native_settings_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined \
  EVIDENCE_DIR=/tmp/native-settings-asan bash test/run_runtime_native_settings_test.sh
SANITIZE=1 SANITIZERS=thread EVIDENCE_DIR=/tmp/native-settings-tsan \
  bash test/run_runtime_native_settings_test.sh
```

The runner builds real dynamic app/provider fixtures, verifies every selected
transitive System source/header against the exact pinned commit before running,
and emits `source-manifest.json` plus one log per case. The manifest includes
SHA-256 hashes of the selected Runtime and System dependency closures, the
Runtime checkout HEAD and the original Runtime base
`be4ff1448a1865716e92cb6246ba342fc68122e4`. It also verifies the atomic ABI header
copies match. Changed or untracked shadowing System dependencies fail closed.

Plain, ASan+UBSan and ThreadSanitizer host runs pass. Leak detection is disabled;
this is not leak qualification. The legacy broker/controller runner is kept
compatible by an opt-in helpers-only include guard in its existing C fixture.
