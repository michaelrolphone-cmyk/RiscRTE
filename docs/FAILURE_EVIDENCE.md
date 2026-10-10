# Bounded native failure evidence (Runtime 0.1.93)

`RISC_NATIVE_FAILURE_EVIDENCE=1` enables an ESP32-S3-only recorder on the pinned
Arduino 2.0.17 / ESP-IDF 4.4.7 framework. The explicit qualification environment
is `esp32s3-16mb-appdata-iq-stage-crash`. All existing environments leave it off.
The opt-in link also requires `--wrap=esp_panic_handler` and retains the
`risc_native_failure_evidence_abi=1` marker. This is a native diagnostic facility,
not a display, recovery mechanism, product feature selection or partition change.

## Copied application contract

`RiscFailureEvidenceV1.h` defines the independent record and copied client.
`RiscRuntimeV1.h` appends only `failure_evidence`; every .92 field offset remains
unchanged. Consumers check `RISC_RUNTIME_FAILURE_EVIDENCE_V1_SIZE` before reading
the suffix, then supply the full client `struct_size`. Existing resident structs
are unchanged. An older/disabled Runtime remains a supported unavailable case.

Only the active, healthy configured default/host in `app_main` obtains a client.
The client carries its invocation token. `read` copies a record and never
consumes it; `OK` can return historical, already acknowledged evidence, so the
consumer checks `RISC_FAILURE_PENDING`. `NONE` is an explicit absence.

The same current host can read after terminal retention using its already copied
client. Acknowledge is forbidden then. Foreground/legacy invocations, init/fini,
foreign tasks, stale copied tokens, a pending handoff and unhealthy custody cannot
acknowledge. Copying a still-valid client is normal; it does not prolong authority.

After successful display completion and the presentation's explicit Continue,
the host calls `acknowledge(invocation, record_boot, record_sequence)`. A wrong
record ID changes nothing. Exact repeated acknowledgement is idempotent. The
record remains readable, but pending presentation is suppressed across fresh
default ELF invocations. A valid RTC acknowledgement also suppresses that record
after subsequent supported warm resets. Merely reading, attempting to draw, or a
failed/incomplete first draw must not acknowledge. Runtime cannot verify an opaque
display surface; the consumer owns the completed-presentation check.

## Evidence, identities and persistence

Kinds distinguish reset-only fallback, native panic, and retained custody.
Registers are meaningful only with `RISC_FAILURE_REGISTERS`; application/phase/
invocation are meaningful only with `RISC_FAILURE_CONTEXT`. `status` is separate
from the native exception. `current_reset_reason` describes this boot;
`captured_reset_hint` is the original capture hint. The synthetic reset-only
record has no fabricated exception frame.

Runtime copies normalized application identity and lifecycle phases before
load/init/main/fini/cleanup/handoff and on resident context restoration. The
eight-step history is a bounded account of last committed activity, not proof
that the named application caused a fault. No app/provider/ELF metadata is read
by the panic hook. The native firmware's 32-byte ELF SHA is copied from the
healthy boot descriptor into internal DRAM before capture is enabled. The target
audit checks the actual firmware binary contains the exact linked ELF digest.
An unfilled descriptor leaves capture unavailable. A different firmware identity
invalidates older evidence; this first format does not bridge firmware updates.
Compact context CRC fields are admitted only alongside the matching full 32-byte
identity anchor in the base area; they never substitute for firmware identity.

RTC image size is exactly 1,280 bytes: two 240-byte activity contexts, a 544-byte
base fault, a 224-byte stack/history extension, and a 32-byte acknowledgement.
Only fixed-width numeric/byte data is retained. Record format combines schema 1
and exact byte size. CRC32 covers the body, including zero reserved bytes.

Writes invalidate magic, write bounded fields, write CRC, issue compiler/CPU
barriers, then publish aligned magic last. Contexts alternate. The base fault is
committed before optional stack walking; the extension and acknowledgement are
separately sealed and must match the exact fault ID. An interrupted extension
cannot erase base evidence. A torn acknowledgement leaves presentation pending.
Two DRAM history banks publish only complete history. A panic interrupting a
breadcrumb can therefore omit its newest step, without exposing partial history.

Record IDs are the captured native boot counter and monotonically increasing
event sequence. Counters stop at their maximum rather than wrap/reuse IDs. The
first retention is preserved; the first later native panic may replace it with
the exception in that same boot. A still-pending prior-boot fault takes priority
over a new retention or panic during its attempted presentation: its base and
extension stay untouched until acknowledgement. Repeated captures of the same kind are blocked in that physical
boot. A separately committed extension is first-only for its matching base.

Software, deep-sleep, panic, interrupt/task/generic watchdog resets admit RTC
records only after format, size, CRC, field bounds and firmware checks. Power-on,
brownout, unknown and unsupported resets discard RTC. Brownout and crash reset
classes without a valid fault yield reset-only evidence in current native DRAM;
acknowledgement of this fallback lasts for this physical boot. A last activity
context can accompany a warm-reset fallback, but never supplies a fabricated
fault PC or stack. Boot initialization and reads do not consume pending records.
A fresh crash reset also produces a fresh fallback when only an acknowledged
historical fault remains. That historical record cannot hide the new reset.

RTC no-init is not flash persistence: power loss, an RTC-domain reset, voltage
failure, changed firmware layout, and corruption can lose evidence. No sleep,
power, partition or startup-pin setting is changed to retain it.

## Pinned panic capture boundary

The wrapper uses the SDK's own `panic_info_t` and `XtExcFrame`, guarded by target,
IDF-version, size and offset assertions. The actual SDK archive contains an
external call from `panic_handler.c.obj` to `esp_panic_handler`; final-link audit
proves that call reaches the wrapper. This is pinned private-ABI interposition,
not a public IDF registration API. Framework changes require renewed qualification.

The SDK reaches this boundary after frame construction and peer-core stalling.
The recorder has no locks or waiting. It snapshots numeric reason fields and
validates a complete internal-SRAM exception frame before copying registers.
`g_panic_abort` is classified before the original handler's abort override;
the reset hint retains TWDT classification when its path uses `abort()`.

At most eight PC/SP entries are collected. The raw exception PC is kept separate
from the SDK's normalized backtrace call-site addresses. Every SDK next-frame
read is preceded by an alignment/range check for its SP-16/SP-12 save area.
Traversal requires increasing SP and stops on bad pointers or invalid PCs. The
configured data-cache region is excluded. PSRAM and RTC-heap stacks are explicitly
unsupported, even though the SDK's general sanity helper can accept the latter.
No stack pointer is trusted merely because it came from a fault frame.

Capture and retention use bounded memory-only loops: no allocator, filesystem,
USB, display, ELF lookup, formatting, or flash string access. Recorder code and
literal pools must link into IRAM, state into internal DRAM, and the journal into
RTC no-init. A per-boot reentry guard protects the first committed capture.
The original panic handler is then called unchanged, preserving its debugger
return, watchdog and restart behavior. It already prints through UART and
USB-Serial-JTAG and attempts a coredump; these existing SDK actions are outside
the added recorder and are not removed by this feature.

Early faults before healthy setup, resets bypassing the panic hook, corrupted
frames/stacks, and abort/watchdog attribution cannot provide universal stacks.
Raw dynamic-ELF PCs are not symbolized by the firmware; decoding them may require
separate relocation/image evidence. A successful link is not hardware qualification.

## Existing coredump support

The pinned SDK enables flash ELF coredumps, CRC32, up to 64 tasks and a 1,024-byte
coredump stack. The selected app-data partition table has no coredump partition,
so the SDK cannot persist such a dump in this layout. A flash coredump deployment
would require a different provisioned layout and panic-time flash writes. This
recorder does neither.

## Verification

- `test/run_failure_evidence_store_test.sh`: CRC/semantic corruption, reset and
  firmware classes, full write-by-write interruption cases, bounds, first fault,
  counters and exact acknowledgement
- `test/run_failure_backtrace_test.sh`: guarded traversal plus the exact frozen
  .92 public Runtime prefix; compile the C prefix test with Xtensa GCC as well
- `test/run_failure_evidence_lifecycle_test.sh`: real loaded host/foreground/
  legacy lifecycle using the actual store, failed first presentation, successful
  acknowledgement, stale tokens, retained read/denied ack and disabled backend
- Existing resident-shell and resident-legacy suites, normally and with ASan/UBSan
- `scripts/audit_failure_evidence.py`: opt-in/off symbols, exact wrapper call,
  complete added capture call closure and literals in IRAM, prohibited-call
  rejection, RTC/DRAM placement, firmware identity and static stack-frame bounds

The auditor deliberately stops at the original SDK panic handler. It reports
per-function compiler stack frames, not a guarantee about a corrupted interrupted
stack or the complete SDK panic path. LeakSanitizer is unavailable under this
executor's ptrace; ASan/UBSan runs use `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`.

Authoritative pinned references: [panic boundary](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/panic_handler.c),
[panic behavior](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/panic.c),
[Xtensa frames/backtrace](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/arch/xtensa/debug_helpers.c),
[reset classification](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/soc/esp32s3/reset_reason.c),
and [coredump partition handling](https://github.com/espressif/esp-idf/blob/v4.4.7/components/espcoredump/src/core_dump_flash.c).
