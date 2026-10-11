# Bounded native entropy backend

Runtime 0.2.3 adds the provider-only `platform.entropy@1` contract, an activation-scoped Runtime broker, and an explicitly selected ESP32-S3 backend. Ordinary target profiles keep it disabled. No product enables sharing, installs a provider, or generates credentials automatically. The optional `esp32s3-16mb-appdata-iq-tcp-entropy` profile selects the linked `risc_entropy_abi=1` marker and existing TCP transport.

## ABI and native boundary

`sdk/driver/RiscEntropyV1.h` defines a version/size/context prefix and one
`fill(context, output, size)` operation. Successful calls fill exactly 1 through
32 bytes; every failure leaves caller output unchanged. No seed, session state,
counter, filesystem storage, heap allocation, or random bytes persist in this
backend. Its fixed 32-byte private stack buffer is erased through volatile stores
before return, including after a rejected sample.

| Result | Meaning |
| --- | --- |
| `OK` (0) | Exactly the requested bytes were copied after all checks. |
| `INVALID` (-1) | Null output, zero/oversized request, or zero native activation. |
| `UNAVAILABLE` (-2) | The supplied predicate could not establish live RF entropy. |
| `CONTEXT` (-3) | Initial wrong/missing owner or stale provider generation. |
| `BUSY` (-5) | Reentry, another native operation, or worker resource ownership; retry later. |
| `RETAINED` (-4) | Ownership was lost after entry; the native fault is sticky. |

`RiscBoot::EntropyBackend` accepts the broker's nonzero activation key. The native
backend checks that key is nonzero; it cannot establish whether a key is live.
The Runtime broker checks the live provider activation, rejects direct application grants, revokes copied tables on unload/reentry, and propagates sticky native retention. This native table is not an ELF
import or a provider dependency table.

`NativeEntropy::configure(ownerTask, rfReady, tryShared, endShared)` installs trusted native predicates
only while idle and not retained. It returns false during a call or after a
retained fault; reconfiguration cannot clear that fault. Both callbacks must be
bounded, synchronous native observations. A native atomic guard is acquired
before the initial owner callback, so recursion from either callback and
concurrent calls fail before reaching RNG or caller output. The native backend's
`idle` conservatively returns false for every activation while any call is active,
and after retention. `safe` becomes false after an observed in-operation owner
loss. There is no production reset function for retention.

## ESP-IDF source requirement

The official [ESP-IDF 4.4 ESP32-S3 RNG documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/system/random.html)
requires an enabled main hardware entropy source for guaranteed true-random
output. A bootloader-provided seed or the S3's always-enabled secondary oscillator
does not suffice for continuous guaranteed entropy. `esp_fill_random` uses
`esp_random`, so the same source prerequisites apply. The bootloader entropy
source also conflicts with later use of RF/ADC and must be disabled before those
uses.

This backend therefore accepts only a trusted predicate for an already enabled
RF source. It neither starts Wi-Fi/Bluetooth nor calls the bootloader entropy or
ADC APIs. False readiness before sampling means no RNG call. After sampling into
private scratch, it checks the owner again, calls readiness again, and checks the
owner after that callback before copying to caller output. Readiness loss rejects
the sample with `UNAVAILABLE`; owner loss rejects it with sticky `RETAINED`.

The native configuration holds the same atomic resource lease used by the asynchronous Wi-Fi SDK worker throughout readiness checks, sampling, and output copying. A worker-owned lease returns transient BUSY before any RNG call. Observed owner loss retains the lease; no cleanup callback runs after that boundary. Readiness uses copied/native state while the lease is held and requires the same started Wi-Fi generation to have reached a healthy link, with no subsequent stop/deinitialization. The established bit is latched; it does not prove current association or network reachability. Those are checked separately by TCP. This port relies on the pinned SDK guarantee while Wi-Fi is enabled, without changing modem power-save settings. It makes no Wi-Fi SDK query and does not treat acknowledged radio-off idle as an entropy source. BLE-only readiness is not selected by this integration. No statistical or physical hardware entropy qualification is claimed.

## Reproduction and evidence

Run the production-header shim and C ABI compile:

```sh
bash test/run_native_entropy_test.sh
SANITIZE=1 bash test/run_native_entropy_test.sh
```

The shim intercepts `esp_fill_random` with deterministic bytes and performs no
host RNG, networking or device actions. It covers all legal lengths, invalid
bounds/null output/zero activation, caller canaries, private SDK scratch, absent
predicates, initial wrong-owner recovery, pre/post RF unavailability and recovery,
owner loss at every observation and inside RNG/readiness, sticky retention,
predicate/SDK recursion, reconfiguration during calls, and a concurrent caller.

For the actual installed ESP32-S3 compiler and real SDK header:

```sh
ENTROPY_TARGET_PREFIX=/path/to/toolchain/bin/xtensa-esp32s3-elf- \
ENTROPY_IDF_INCLUDE=/path/to/sdk/esp32s3/include/esp_hw_support/include \
bash test/run_native_entropy_target_compile.sh
```

Local qualification on 2026-10-10 used Xtensa ESP32-S3 GCC 8.4.0
(`esp-2021r2-patch5`) and the real Arduino 2.0.17 / IDF 4.4 SDK header. C ABI and
C++ production-object compiles passed. The object imports only `esp_fill_random`,
`memcpy`, and `memset`; it requires no out-of-line atomic support. The compiler
reports a 96-byte stack frame for `fill`, excluding its callees. Normal host and
ASan/UBSan checks passed. This executor prevents LeakSanitizer under ptrace, so
the sanitizer run used `ASAN_OPTIONS=detect_leaks=0`; leak checking was not run.

The Runtime/CpuPort suite checks zero-I/O metadata admission, missing/native-disabled refusal, provider generations, direct raw application denial, unchanged caller output on every backend failure, shared-resource BUSY, owner loss and terminal cleanup. With SYSTEM_APPS set, it loads the real ordinary crypto.entropy provider through the actual Runtime graph and confirms transient BUSY does not poison its lifecycle.

```
SYSTEM_APPS=/path/to/matching/System-Apps bash test/run_entropy_runtime_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SYSTEM_APPS=/path/to/matching/System-Apps bash test/run_entropy_runtime_test.sh
TSAN=1 bash test/run_native_entropy_test.sh
```

Target linkage and physical qualification are recorded separately; a source-only checkpoint does not claim a product rollout or device test.
