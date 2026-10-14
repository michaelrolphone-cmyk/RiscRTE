# Disconnected native atomic KV endpoint

The additive `sdk/app/RiscKeyValueAtomicV1.h` is `storage.key-value.atomic@1`.
It is separate from `storage.key-value@1/@2`; no existing layout, put semantics,
provider routing, shared_settings selection or product broker registration is
changed. Its matching System header is byte-identical.

`NativeAtomicKeyValue` is a native-only namespace-bound materializer around
NativeReplicaCore and a permanently constructed NativeReplicaJob. The app table
has state/limits, revision-checked get, submit, collect and cancel. No callback
accepts a caller-supplied namespace. Inputs are copied into one bounded native
payload; no app callback or app pointer is used by the native worker. All NVS /
AppData publication occurs inside the admitted workerStep execution predicate.

The supplied native Guard is mandatory. Its owner/worker predicates verify the
actual execution context. Its mandatory worker-only finish callback clears the
bound CheckedNativeReplicaStore operation buffers and verifies checked store
exit safety before a completion is handed to the owner. Failed cleanup retains
the native gate and invocation. Its shared namespace acquire/release gate must be
respected by every caller of the core, including SD consumers and legacy
adapters. The gate is held across worker execution and released only on terminal
owner collection. Reads and state calls return Busy while the job owns the core.
Without this shared broker-level custody integration, this materializer must not
be registered or selected in a product. Keep the object constructed between jobs
and fence scheduler access before destruction. Failed native custody remains
retained, never implicitly freed.

Limits: at most 16 mutations, 2048 bytes per value, 1024 intent bytes, 4096 native
group bytes. The full copied payload also fits NativeReplicaJob's 4096-byte
buffer. Duplicate keys, invalid key grammar, shape errors and over-capacity
groups are refused before native I/O. State/get inspect only serialized native
RAM. A stale expected revision cannot mutate the core. Native durable success
publishes the complete map and opaque intent together; it does not imply SD
acknowledgement. Runtime remains schema-blind.

Cancellation changes only the pre-claim state. It never calls
StorageWorker::cancel because that discards completed results. A cancelled
queued job still executes its native mailbox callback and produces a terminal
cancelled receipt without mutation. Once the worker claims publication,
cancellation cannot suppress the terminal outcome. Successful receipts include
native revision and operation ID, even if cancellation raced publication.
Uncertain publication is terminal and is not automatically replayed; native
recovery must reload and verify the core before subsequent use.

## Optional permanent worker binding

[The native settings worker](NATIVE_SETTINGS_WORKER.md) now composes this
endpoint with a shared namespace/background gate and a default-off ESP32-S3
FreeRTOS task. Its native registration and permanent-lifetime requirements do
not select a private mount or product. An optional native Guard::retained
predicate propagates the coordinator atomic retention latch into this endpoint
and its native lifecycle predicates without reading mutable worker/store fields.

## Verification

- `bash test/run_native_atomic_checked_store_test.sh`
- `bash test/run_native_atomic_key_value_test.sh`
- `SANITIZE=1 bash test/run_native_atomic_key_value_test.sh`
- `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash test/run_native_atomic_key_value_test.sh`
- `SYSTEM_ROOT=<System atomic source> bash test/run_native_atomic_wifi_bridge_test.sh`
- `bash test/compile_native_replica_target.sh <PlatformIO packages> <output>`

The actual core tests cover copied key/value/intent lifetime, owner/worker
separation, Busy reads, stale and capacity refusal, same-value explicit intent,
DELETE, pre-claim cancellation, post-commit cancellation, a threaded cancellation
during native publication, and 21 before/after/torn backend failure points.
Reload sees either the complete old map and intent frontier or the complete new
map and new intent, including six uncertain-result cases.

The cross-repository bridge compiles System's actual C PortableWifiAtomic helper
and profile codec, then links them with the actual native adapter/Core. It
checks count/chunk/selector changes, compatible WFI0001 expected-record SHA256,
Save/Forget, stale rejection, cancellation and native reboot. System separately
tests the actual Wi-Fi controller and deferred Remember sequence.

Xtensa compile-only evidence is not firmware, hardware, flash or SD data-path
qualification. No boot, migration selection, provider registration or production
service lifecycle is enabled by this checkpoint.

The checked-store test executes the real CheckedNativeReplicaStore, AppDataFiles
and NvsReplicaMetadata with host NVS hooks. A thousand owner collect polls
perform zero NVS/filesystem calls; publication and terminal store cleanup run
only in the admitted worker. A fresh native instance reloads the complete map
and pending intent. LeakSanitizer is disabled under ptrace supervision; address
and undefined-behavior instrumentation remain enabled.
