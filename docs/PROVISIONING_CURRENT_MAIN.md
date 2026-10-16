# T023 provisioning on the native SDK6 Runtime

This is a source-integration checkpoint based on Runtime main
`c69e7b9b302ba7880ba91a34922cfc1622b082c0` (Runtime 0.4.6). It is not a
product release, full firmware-link qualification, or hardware qualification.
Product versions and release recipes are owned by the product repositories.

## Provenance and composition

The preserved T023 Wave implementation at
`136851d323f4a3ff9cbd26a56aa385d5b5acd56c` (tree
`c6ec9996b802fa0ddd3b4052eb8211016ede4083`) supplies the bounded inventory,
request queue, preserving policy, content-addressed mirrors, private HTTP time
suffix and ordinary-boot gate. This checkpoint applies those features to current
main; it does not replace main with that older firmware branch. Historical
frozen NVS evidence in that lineage remains historical evidence only.

Current Runtime behavior retained during composition:

- All serial transfer/storage source, explicit namespace selection and default-off
  settings remain unchanged. No product enables provisioning or serial implicitly.
- Generic `storage.files` image/metadata routing, direct-to-destination image
  reads, retained sessions, managed ELF admission and source custody survive.
  Internal image fallback uses that same current generic file loader.
- Current settings providers and configured namespaces are retained; the obsolete
  embedded settings-service architecture is not restored.
- SDK6 TLS socket custody, allocator policy, certificate verification and current
  radio/IQ behavior are retained.
- Public hardware and existing ABI prefixes are unchanged. Request and HTTP-time
  suffix headers match their preserved T023 definitions.

The additional request selection is `RISC_NATIVE_PROVISION_REQUEST=1`, requiring
existing paired storage. Its default is zero. The native CMake recursive source
selection includes `RequestQueue.cpp`, `PreservingPolicy.cpp`,
`NativeProvisionRequest.cpp` and `NativePackageVolumeAdmission.cpp`; disabled
native request compilation produces an empty object. No endpoint or product
configuration is activated by adding these sources.

## Current-source adaptations

The preserving-policy helper now receives the immutable `RuntimeFileSources`
map explicitly. Active and staged metadata are read through the same retained
provider authority as the current bank adapter. This is a private C++ helper;
no ELF ABI or public capability layout changes.

Native request hashing and content-digest checks use the existing `RiscCrypto`
SHA256 adapter: PSA on SDK6, the retained mbedTLS route on the legacy build.
NVS request metadata enumeration uses each SDK's actual iterator interface;
SDK6 allocation, enumeration and metadata failures fail closed without treating
unavailability as absence. The private `rte_provreq` namespace and three-key
selection protocol are unchanged. Mutation ambiguity still quarantines all
native NVS/PHY clients until ordinary restart; no format, namespace erase or
same-boot recovery is added.

The 160-file bound applies only to explicit provisioning inventories. Ordinary
JSON array limits remain unchanged. Internal-only installation is an explicit
selection. Boot-owned SD mirrors retain explicit immutable internal fallback,
exact copied-image content verification and all cleanup barriers. Deep-sleep
wake defers queued provisioning without reading/modifying pending input or
changing retained RTC time. HTTP's initial private time sample never writes
wall time; it is unauthenticated SNTP, not authenticated NTS.

## Reproduction and limits

Run the focused host suites with `TMPDIR=/tmp`; sanitizer variants select
`SANITIZE=1`. In ptrace-based executors LeakSanitizer cannot run; use
`ASAN_OPTIONS=detect_leaks=0` there and report ASan/UBSan separately from leaks.

- `test/run_provisioning_test.sh`
- `test/run_request_queue_test.sh`
- `test/run_request_queue_compaction_test.sh`
- `test/run_native_provision_request_test.sh` (legacy and SDK6 adapter signatures)
- `test/run_preserving_policy_test.sh` (current generic-file providers)
- `test/run_removable_runtime_test.sh`
- `test/run_http_test.sh`
- `test/run_native_http_idf6_test.sh` (set `IDF_PATH` to the pinned SDK)
- `test/run_native_provision_boot_gate_test.py`
- `test/native_nvs_shim/run_test.sh`
- `test/run_nvs_quarantine_radio_test.sh`
- `test/run_native_bank_test.sh` with an explicitly validated `BOOTLOADER_FILE`,
  `NATIVE_PROVIDER_TEST_ABI=2` and the desired `APP_DATA_TEST` selection

Actual-SDK target-object qualification is reproducible with:

```
python3 test/compile_provisioning_idf6_target.py \
  --compile-commands /path/to/configured/native/compile_commands.json \
  --idf /path/to/pinned/esp-idf \
  --output /tmp/provisioning-sdk6-objects
```

This script checks the SDK/GCC pins, rebinds every Runtime source/include to this
checkout, records source/object hashes and commands, compiles request selection
and stage-logging variants, and checks that HTTP imports contain no global-clock
write or SNTP/RTC service. Compilation is single-job.

A final product-native link, exact product-store admission, immutable release
assets/feeds and SDK6 real-flash NVS fault-injection qualification remain separate
gates. The historical 4.4.7 frozen NVS matrix is not relabeled as SDK6 coverage.
No physical boot, SD removal, wake, rollback or power-cut test is claimed.
