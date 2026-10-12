# Source-bound Watch deep-wake measurement

The delivered Runtime `1ecb091ae65f1cf202e034ba0a90860b24f9d493`
already avoids immutable installed-image rescans and has bounded JSON/ELF read
cooperation. A remaining reported seven-second wake is not explained by those
previous fixes. This profile supplies existing timing evidence without changing
boot, admission, provider lifetime, storage layout or sleep behavior.

```sh
pio run -c crown-wake-profile.ini -e crown-wake-profile -j 1
```

The profile inherits the complete crown-recovery target and adds only
`RISC_STAGE_LOGS=1` and `RISC_PERFORMANCE_TRACE=1`. Ordinary targets are unchanged.
Stage output is automatic, bounded and nonblocking; it does not wait for USB.
The existing 8 KiB HWCDC transmit ring may lose output if no host drains it.
The separate fixed-size performance recorder preserves aggregate durations when
its event ring overwrites early records. The existing `perf` command retrieves
that evidence after startup. See [stage logs](STAGE_LOGS.md) and
[performance tracing](PERFORMANCE_TRACE.md).

Use setup/bank/mount/app-data/provisioning/metadata/app-load/app-entry/provider
boundaries to locate the interval. If ELF load dominates, performance phases
40, 42 and 45 distinguish file read, map/relocation, and structural parse.
Intervals are inclusive and nested; never sum them as independent durations.
Product first-frame markers are separate from Runtime app entry. ROM/bootloader
time precedes setup, and software presentation is not physical visible pixels.

## Exact delivered-store reproduction

The existing production JSON-reader test against the decoded Watch 1.1.12
store reads 58 files, 101,100 bytes and 283 chunks. With zero injected I/O time
it requests 59 scheduler ticks. Slow I/O can trigger more bounded checkpoints.
The existing bank test against the delivered 1,417,808-byte firmware and ABI2
store reads only the 96-byte selected commit record: no hash, image scan, SDK
image verification, or scheduler delay in bank selection.

The pinned target has 1 kHz RTOS ticks. No multi-second fixed native startup
wait was found in the installed-boot path. These source/host observations do not
attribute the reported physical delay or establish improved wake latency.

## Qualification

`test/test_crown_wake_profile.py` checks inheritance and limits added flags to
the two existing observers. The dedicated workflow exercises bounded transport,
recorder/replay, stage/performance lifecycle composition, crown recovery,
light/deep sleep, metadata deadlines and retained-close admission. Its target
job builds this exact profile with one job and runs the existing mandatory IQ
static DRAM/IRAM reservation proof. The diagnostic allocations reduce memory
headroom; inspect the exact linked ELF and proof before packaging it.

This is a measurement candidate, not a measured latency fix or hardware
qualification. It does not assemble, flash or release a product image.
