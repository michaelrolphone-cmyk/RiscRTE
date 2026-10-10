# Startup delivery correction

The Runtime 0.1.61 source at `7e576bcd` enlarged HWCDC's TX ring to 8192 bytes,
but `RiscDiagnostics::line()` still discarded a statement whenever
`bool(Serial)` was false. That check occurs before capacity admission. HWCDC's
boolean represents CDC connection readiness, not whether its allocated TX ring
can accept text. Consequently an empty ring could not preserve startup messages.

The ordinary paired/app-data path emits 13 statements before
`boot manifest-prepare end`: USB capacity, boot begin, source identity, two bank
selection statements, two mount statements, two app-data statements, two
provisioning statements, the provisioning result line, and manifest prepare
begin. Those 13 statements can occupy at most 3328 bytes under the existing
256-byte line bound, less than the 8192-byte allocation. This explains the
reported early `lost=13` pattern; increasing capacity alone did not remove the
connection gate.

## Before/after production-source reproduction

The same 13 representative statements were submitted to the hash-pinned,
unmodified Arduino 2.0.17 HWCDC implementation, with its allocated TX ring and
no ready CDC host. Both tests used the production Runtime adapter. The before
test compiled the exact adapter from `7e576bcd`; the after test compiled this
correction.

- Before: 0 bytes queued, all 8192 bytes free, followed by automatic
  `RTE_LOG lost=13 truncated=0` when the host became ready.
- After: all 13 statements queued in original order (697 bytes), with 7495 bytes
  still free. When the host became ready, the original timestamped statements appeared
  automatically before the manifest-prepare endpoint, with no loss notice.

This reproduces the software failure with available capacity and isolates the
connection guard. It does not independently measure the physical device's USB
enumeration timing.

## Change and bounds

Only stage builds using HWCDC bypass the disconnected-host rejection. They
continue to call the driver's connection probe and use its existing TX ring.
Complete-line and per-write capacity checks remain in place. As the adapter is
the sole producer and ISR draining only frees capacity, disconnected writes do
not enter the driver's FIFO-eviction branch. Each write remains at most 64 bytes,
with zero timeout and no host-dependent delay. There is no new buffer, command,
recorder, thread or boot dependency. Ordinary Watch builds without
`RISC_STAGE_LOGS` retain their prior disconnected-host behavior and 256-byte ring.

The ring is finite. Later lines are dropped and counted if it fills; older
startup text remains intact. Reset and the existing recovery `Serial.end()` can
discard pending ring bytes. This change does not promise delivery of arbitrary
bursts, delivery without a host, or retention across sleep.

## Validation

- `bash test/hwcdc_pinned/run.sh`: actual pinned driver with stage flag on/off,
  retained diagnostics on/off, delayed CDC handshake, exact 13-statement
  startup, undrained roughly 6 KiB burst, full connected/disconnected rings,
  oldest-byte preservation, bounded no-host behavior, allocation fallback,
  recovery capacity, ownership and zero positive waits/delays.
- The pinned driver suite under ASan/UBSan.
- `bash test/run_stage_log_test.sh`, normal and ASan/UBSan: automatic output,
  bounded formatting, short/zero writes, loss notices, owner/reentry, explicit
  dump arbitration and disabled macro behavior.
- Existing diagnostic journal, USB recovery and native performance replay suites.
- `esp32s3-16mb-appdata-iq-stage` single-job target compile with telemetry disabled.

LeakSanitizer is disabled under this environment's process tracing; ASan and
UBSan remain enabled. These are software qualifications, not hardware results.
