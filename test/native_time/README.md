# Exact native microsecond-to-millisecond conversion

`NativeTimeQuotient.h` is an internal reusable arithmetic helper, not a Runtime or provider ABI. Native `RiscPlatform::millis()` still reads `esp_timer_get_time()` exactly once, converts signed-to-unsigned exactly as before, floors /1000 and returns the low32 bits. No caching, changed epoch, reduced precision, new error handling or callback removal. The non-native Arduino branch is unchanged.

## Proof

Let B=65536 and n=h*B^2+a*B+b, where h is32-bit and a,b are16-bit. For h=1000*k+r, the high quotient term k*B^2 vanishes modulo2^32; retain r=h%1000. Divide r*B+a to obtain quotient digit q1 and remainder r1, then divide r1*B+b to obtain q2. Each dividend <=65,535,999, each remainder <1000 and each quotient digit <65536. Thus `(q1<<16)|q2` equals the existing uint32 quotient for every uint64 input, without overflowing any intermediate.

Selected Xtensa GCC15 lowers each32-bit /1000 to `muluh` by M=0x10624dd3 followed by shift6. This is exact:1000*M=2^38+56 and56*x<2^38 for every uint32 x; rounding error above x/1000 is strictly below1/1000, so cannot cross a quotient boundary. No hand-coded target assembly or compiler attribute is required.

## Owning checks

```
python test/native_time/run.py --output /workspace/.../native-time-evidence
python test/native_time/run.py --output /workspace/.../native-time-evidence \
  --compile-commands /workspace/.../native/build/compile_commands.json
```

Four host lanes: native/Arduino, normal/ASan+UBSan. Each verifies12,274,104 full64 values (including10million deterministic random inputs),65,536,000 limb recurrence inputs and13 signed timer endpoints. Native clock adapter is checked for exactly one timer call; legacy Arduino adapter is checked independently. Boundary cases include every power of two, quotient wrap, zero and negative signed source values. No timing benchmark is inferred from host execution.

The optional target check compiles actual `src/main.cpp` using the preserved native recipe, relocating its source include root to this checkout and retaining the supplied external dependency roots. It hashes actual compiler dependency files and checks the actual monotonic callback and shared millis/helper code for absence of `__udivdi3`. This is a compile/disassembly check, not a link or hardware qualification.

In the selected full main TU, -Os naturally shares millis out-of-line:15-byte callback plus83-byte shared millis body (excluding literals), with one call to shared millis and one timer call. The old callback was26 bytes and directly called timer plus ROM division. Unlike an isolated forced-inline comparison, total firmware size and call overhead cannot be inferred by multiplying a per-call-site delta; final linker sharing and other users matter. Three native muluh operations replace the general divider. No measured microsecond or FPS savings are claimed.

Also run existing platform contracts using the selected IDF tree and workspace TMPDIR:
```
IDF_PATH=... TMPDIR=... bash test/run_native_platform_test.sh
IDF_PATH=... TMPDIR=... SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_native_platform_test.sh
```

## Consumer audit at parent273bdbccf65647a803ab0586a418f8283732baec

Direct NativeTime includes are NativePlatform.h and NativeBankStore.cpp. All production RiscPlatform::millis callers are:
- `src/main.cpp`: monotonic callback, health uptime, settings lifecycle time callback;
- `NativeBankStore.cpp`: admission/install elapsed-time limits, bounded cooperative yields, provisioning/read clocks;
- `NativeSerialFiles.cpp`: serial-file endpoint time callback;
- `OwnerMaintenance.h`: maintenance endpoint clock;
- `SleepDiagnostics.cpp`: diagnostic events/checkpoints and elapsed timestamps.

`NativePlatform.cpp` includes the platform surface but does not itself call millis. Existing `test/native_platform_test.cpp` exercises the platform surface. These consumers retain their exact time origin,32-bit wrap/difference arithmetic and independent safety/custody guards. No direct esp_timer callers or unrelated clocks are migrated. Tests do not imply universal application adoption or hardware qualification.
