# Dependent sleep runner integration correction

Only `test/native_sleep_transaction.py` changes. Production Runtime remains
byte-identical to the qualified lifetime repair. No native-idf6 held fixture or
workflow was edited.

The existing CI workflow pins Wave source
`26ebe127fc2f6ea1a3e852ca4571fa70469bdfb1`. The same exact local git objects were
archived to an isolated input directory; its consumed source hashes are recorded.

## Reproduction and correction

The baseline generated sleep harness failed linking `serial_littlefs_start`,
`serial_littlefs_preserved` and `serial_littlefs_stop` because the shared Wave
fixture now owns an independent real LittleFS namespace 2. The runner now links
the same pinned LittleFS composition, selection flags and VFS wrapping as its
primary consumer. It no longer links a duplicate NativeSerialStorage object.
These lifecycle functions were not replaced by success stubs.

Once linked, cold/absent/healthy sleep passed. The abandoned case exposed a
stale assertion expecting BUSY after `Runtime::run` returns retained. The firmware
binding now explicitly retains client custody at that boundary;
`RuntimeFiles::call` returns RETAINED before examining its still-frozen sleep
state. The correction requires the granted client to remain bound and retained,
its native grant and sleepFrozen state to remain present, open to return exactly
RETAINED with no token, and checkpoint/final stop to refuse without any provider
I/O, quiesce or stop. All existing BUSY assertions during reversible in-app
transactions remain unchanged.

Both failures and their runner versions are preserved. The baseline traceback's
printed source excerpt reflects the later edit while the process linked; the
actual linker command/error and saved baseline runner identify the failing input.

Repository-wide source search found only this runner and the already-correct
`run_native_serial_waveshare_sd_test.py` consuming the shared C++ fixture. The
post-return Python client consumes the executable and has no link dependency.

## Final checks

Both commands exited 0, each running cold, absent-card, healthy and abandoned:

```
python3 test/native_sleep_transaction.py --product /workspace/shared/serial-lifetime-evidence/sleep-ci/provider --output /workspace/shared/serial-lifetime-evidence/sleep-ci/final-normal
python3 test/native_sleep_transaction.py --product /workspace/shared/serial-lifetime-evidence/sleep-ci/provider --output /workspace/shared/serial-lifetime-evidence/sleep-ci/final-sanitized --sanitize
```

The runner sets ASAN_OPTIONS=detect_leaks=0 and UBSAN_OPTIONS=halt_on_error=1;
address/undefined sanitizers remain enabled, no leak-detection claim. Raw logs,
complete compiler commands, original/generated source hashes are included.
Every recorded final source hash was rechecked before commit.

Actual Runtime, CpuPort, NativeSdmmc source, native storage custody selection and
extracted native deepReady checks remain in the test. It models terminal Deep by
throwing from the hardware callback and checks reversible resident preparation
plus Deep refusal. This is host evidence; actual CPU Light sleep, the complete
paired-bank/AppData Deep-readiness path, target hardware and physical media are
not qualified. No device access occurred.
