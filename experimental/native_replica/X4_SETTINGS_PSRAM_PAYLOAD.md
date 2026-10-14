# Default-off X4 settings payload allocator

`NativeSettingsPayloadAllocator::allocator()` supplies the existing
`NativeReplicaCore::Allocator` callbacks. It is available only under explicit
`RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1` and `BOARD_XTEINK_X4_PRO`; the implementation
also requires ESP_PLATFORM. The default-off target object is empty. No product,
Core, store, filesystem or catalog automatically chooses this allocator.

The caller may pass the returned value through existing Core, checked-store,
AppDataFiles, synchronization and catalog allocator inputs. Every participating
payload allocator must be passed explicitly. AppDataFiles has context-free
allocation/free hooks, so it needs small adapters that call the returned
Allocator callbacks with their context; the Allocator value is not directly
assignable to those hooks. Supplying Core alone does not
reroute catalog/readback/file buffers or allocations made by NVS, filesystem,
crypto, drivers, SDK, C++ containers or other tasks. Payloads are byte-addressable
PSRAM, requested using exactly `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. There is no
INTERNAL fallback. A missing PSRAM heap, failed capability request, arithmetic
overflow, or returned range outside external RAM fails with a null pointer.

## Allocation and release contract

Payload alignment is at least 16 and at least `alignof(std::max_align_t)`.
The exact .99 ESP32-S3 compiler gives alignment 16, header 16, and a fixed SDK
request overhead of 31 bytes per successful payload allocation. The returned
Allocator value is 12 bytes, already counted in existing controls that copy it.
The implementation owns no static data or permanent allocator control object. An N-byte request
therefore needs one contiguous PSRAM block of N + 31 bytes, before SDK allocator
metadata/rounding. The header records the original allocation, payload length
and complete requested length. A zero-byte request still requests the overhead
and returns an aligned pointer if successful. Addition overflow is refused
before calling the SDK. Pointer/range overflow is refused before payload use.

Only the matching pointer returned by this allocator may be released, exactly
once after its consumers have quiesced. Null release is harmless. Release uses
Core's volatile wipe over the entire original allocation: payload, header and
all alignment slack, then calls heap_caps_free on the original pointer. A
rejected vendor allocation is freed before any caller data is written into it.
Release wiping takes linear work in the full allocation length; its target
elapsed time is unmeasured. The callbacks are stateless; underlying heap synchronization remains the SDK's
responsibility. This is not an allocator for arbitrary over-aligned objects.

## Placement limits

Do not use this allocator for permanent worker stacks/TCBs, coordinators, atomic
or native-client controls, Core/store objects themselves, retained lifecycle/
maintenance/bootstrap controls, DMA buffers, ISR or cache-disabled access.
Those objects retain their existing placement requirements. Core and store
payloads can outlive the submitting caller, so all selected code paths must be
PSRAM-safe through their actual lifetime, including SDK storage/cache activity.
PSRAM capacity, largest available block, fragmentation, concurrent radio/
display/SD pressure and actual physical placement are hardware prerequisites.
Failure remains a normal allocation failure through each existing caller's
error/retention handling; this allocator does not authorize any rollback or
release of retained native custody.

## Reproducible checks

```
bash test/run_native_settings_payload_allocator_test.sh OUTPUT
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_native_settings_payload_allocator_test.sh OUTPUT
python3 test/compile_native_settings_resource_target.py COMPILE_COMMANDS OUTPUT --payload-only
```

The host shim checks exact capability flags, deliberately misaligned vendor
results, zero/small/large allocations, pre-SDK size overflow, returned-address
overflow (both alignment and allocation-tail guards, before the external-RAM
predicate), SDK failure without fallback, internal/range-tail rejection, complete
wiping before free and 2,000 allocations across four concurrent callers. Both
normal and ASan/UBSan pass. The 64-bit host header/overhead are 32/47 bytes and
are not the target layout. Target compilation uses the actual .99 cohort
command, compiles the selected implementation and asserts an empty default-off
object. Evidence is in `evidence/native-settings-resource-budget/payload-*`.
These tests qualify the callback contract, not physical PSRAM, complete caller
fault recovery, hardware high-water, SD latency or whole-product RAM admission.

The existing representative payload report measures requested payload bytes
with an independently instrumented allocator. Its modeled default eight-byte
prefix is not this allocator's 31-byte target overhead. For this allocator,
add 31 times the simultaneously live allocation count, then separately account
for SDK metadata/rounding and fragmentation; do not reinterpret prior host
payload peaks as device heap measurements.
