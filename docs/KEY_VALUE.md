# Bounded app key-value storage v1

Canonical C header: `sdk/app/RiscKeyValueV1.h`. Capability
`storage.key-value@1` is an optional native app service, separate from
`storage.volume` and the immutable boot filesystem. Firmware0.1.4 adds it on top
of B1 without changing light/deep-sleep code or their headers.

An app declares `{"capability":"storage.key-value","api":1}` in its manifest.
An explicit boot policy grants that same capability with a positive
`instance_id` in1..2147483647. This ID selects a storage namespace; it need not
name hardware. Apps intended to share data are explicitly provisioned with the
same ID. Different IDs are isolated. Firmware0.1.10 permits multiple distinct
positive namespace grants for the one declared storage.key-value@1 requirement,
within the unchanged eight-grant total per app. Duplicate namespace grants fail
admission. Acquire accepts an exact authorized ID; ID0 succeeds only when one
namespace is authorized and rejects ambiguity without selecting a namespace. The application never supplies a namespace to get/put.
Undeclared, zero-namespace, unsupported-version, unavailable-backend and extra
boot grants fail admission before any app runs. Policyless children inherit no
capabilities.

## Values and failures

Keys are1..15 ASCII characters from `[a-z0-9_.-]`; blobs are1..64 bytes. There is
no delete, list, path, migration, defaults, app mode, display, reset, format or
partition-management operation. Apps own value encodings, version checks and
fallback policy. An invalid or unreadable value must never be confused with a
successfully loaded value.

- OK0 means a complete valid read or committed write with exact readback.
- NOT_FOUND-1 means the SDK reports a missing namespace/key. Returned
  zero-length/oversized data and SDK-reported errors other than NOT_FOUND give
  IO-5. SDK lookup/recovery can hide corrupt entries, type mismatches or other
  faults behind NOT_FOUND; this API cannot distinguish those from true absence.
  Both require the app's safe fallback.
- BUFFER_SMALL-2 reports the needed length and writes no data. NULL/0 is a valid
  get probe; present data gives BUFFER_SMALL. NULL/nonzero is INVALID-3.
- out_size is mandatory and becomes0 on every error other than BUFFER_SMALL.
- CONTEXT-4 denies calls outside the active owner invocation/live grant.
- IO-5 from put means the durable outcome is uncertain: either old or new data
  may be present. It does not promise rollback. Apps must not display success.

All tables expire on release/revocation, including automatic revocation at app
exit. Copied callbacks with old contexts fail even after their slot is reused or
another Runtime is created: pointer-sized opaque generation tokens are never
reused within the process and exhaustion fails closed. These tokens are never
dereferenced. This is lifecycle enforcement for trusted native code, not a memory
sandbox or protection against native code that reads other runtime memory.

## ESP32-S3 backend

A compiled-in optional RiscBoot::Port backend receives the already selected
namespace ID. It generates `rte%08x` names (11 ASCII bytes) for the existing NVS
partition. Reads open read-only, validate length and read to bounded scratch
storage; no error exposes partial caller data. Writes use nvs_set_blob then
nvs_commit and exact readback through a separate read-only handle. In pinned
IDF4.4.7, NVSHandleSimple::commit validates the handle and returns success;
nvs_set_blob performs the storage work directly. Calling commit preserves the
public SDK protocol, but does not add a separate physical durability barrier. Full storage,
commit failure, readback error or mismatched bytes returns IO; no destructive
recovery is attempted. Existing NVS partition layout is unchanged.

Arduino2.0.17's [initArduino implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-misc.c)
otherwise erases the entire NVS partition when nvs_flash_init returns
ESP_ERR_NVS_NO_FREE_PAGES or ESP_ERR_NVS_NEW_VERSION_FOUND. The normal port now
wraps that call, initializes only once and latches its real status. Those two
codes are returned to Arduino as generic failure, preventing its erase branch;
the backend retains the exact failure and performs no operations. Other errors
also remain failures. Startup reports unavailable NVS and can still boot apps,
which receive IO and choose their own safe default. No erase, retry, repair or
implicit formatting is introduced. NVS initialization may perform the SDK's own normal metadata bookkeeping even
before returning a failure; suppressing Arduino's partition erase is not a
promise of byte-for-byte preservation or a read-only medium.

Embedded cam-ci/x4-ci builds retain their existing no-NVS initialization wrapper
and do not expose this backend. The normal wrapper/adapter are compiled out for
those builds. No device, flash, deployment, release or partition migration is
part of this change. Persistence across reboot is distinct from persistence
across reflashing; an image that overwrites NVS can destroy values.

Pinned implementation: [NVSHandleSimple](https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_handle_simple.cpp).
ESP-IDF API reference: [NVS library](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/storage/nvs_flash.html).

## Verification

`bash test/run_key_value_test.sh` runs real production Runtime with dynamic app
ELFs, explicit shared/isolated namespaces, handoff and a second fresh process.
It checks declaration/namespace/version denial, owner guards, copied stale
contexts, automatic revocation, 16-grant bound, non-reusing generation exhaustion,
key/blob boundaries, probes and backend error/partial/corrupt responses.
The native NVS SDK-shim suite exercises the actual adapter and initialization
wrapper, with faults at open, size/read, set, commit and readback, full storage,
uncertain persistence on write/commit errors and both destructive-recovery
trigger errors. The shim models pending writes/commit outcomes to fault-test the
public protocol; it is not the pinned SDK's physical commit model.
Run the same suite with SANITIZE=1 for ASan/UBSan.

Host tests, SDK shims and CI target builds are not physical durability, power-loss,
flash-endurance, wake or current-draw qualification. Simulated commit ambiguity
is a contract test, not an empirical flash power-cut experiment.
