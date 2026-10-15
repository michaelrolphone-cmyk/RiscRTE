# Native diagnostic workspace placement

The SDK6.1 native ESP32-S3 build keeps the complete sleep replay, ordinary copied
checkpoint and performance replay in fixed external BSS when the selected SDK
configuration enables `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`. No heap
allocation, reduced history, optional diagnostic loss or IQ-bank relocation is
used. The legacy adapter retains its original static objects and placement.

## Why these objects can move

The storage is private to `src/ports/esp32s3/SleepDiagnostics.cpp`:

- `Replay` owns a complete copied RTC-journal snapshot, a complete copied
  checkpoint, bounded command/line buffers and owner-task replay cursors.
- `Checkpoint` owns the ordinary latest checkpoint, including the unchanged
  192-byte application identity and 768-byte text capacities. It is deliberately
  separate from reset-retained RTC evidence.
- `RiscPerf::Replay` owns its complete copied performance snapshot and bounded
  parser/output state. Every phase counter and selected history slot remains.

No object contains a control atomic, DMA buffer, borrowed provider allocation,
interrupt callback or flash-disabled obligation. They are accessed only from the
ordinary diagnostic owner's cache-enabled call path after `start()`. The native
provider checkpoint entry rejects ISR callers before accessing its copy.
`poll()` retains the shared 16-byte input/64-byte output budgets, snapshot
arbitration and short-write handling. Neither disconnect nor a USB PHY lease
releases the backing memory or drops its custody.

The call-site audit covers the actual native paths:

- `main.cpp::setup()` starts diagnostics after SDK startup and console creation,
  before the Runtime uses the adapter. No global constructor accesses the
  relocated objects.
- `NativeSleep::lightEnter()` records before `esp_light_sleep_start()` and
  invokes `lightReturn()` after that synchronous SDK call has returned. The
  workspace is not a wake stub or cache-disabled sleep callback.
- `NativeSleep::enter()` records before deep sleep. The ordinary checkpoint and
  replay are freshly reconstructed on reboot; only the separate journal retains
  its existing validated RTC policy.
- `NativeRadioIq` traces on the owner path around synchronous PHY operations.
  Its timer callback and capture task do not touch these snapshots.
- `NativeFailureEvidence` retains its own DRAM/RTC state and never uses the
  replay workspace for panic capture.

The RTC `Journal` remains 1,288 bytes with its unchanged validation/retention
rules. Diagnostic owner identity, output/reentrancy flags, USB lease fences,
recovery state and loss counters remain internal. The live performance recorder,
CPU object, settings control/atomics and all ISR/cache-off state are unchanged.

## Initialization and lifetime

The audited SDK is Espressif ESP-IDF v6.1, commit
`fff9895c82d744c7237be8847347bdd1b07c6643`.

In that SDK:

- `components/esp_system/port/cpu_start.c` initializes PSRAM and aborts if it
  cannot satisfy the enabled external-BSS requirement. It invokes
  `esp_psram_bss_init()` before application startup.
- `components/esp_psram/system_layer/esp_psram.c` maps external BSS, subtracts
  its whole span from the external heap and zeroes the section.
- `components/esp_system/ld/esp32s3/sections.ld.in` collects `.ext_ram.bss.*` in
  the external `NOLOAD` region.

The application stores an aligned byte array in that BSS section and constructs
`ReplayWorkspace` in-place inside `start()`, before publishing the diagnostic
owner. This is essential: performance snapshots have nonzero defaults, including
sequence IDs and `UINT64_MAX` for the overhead minimum. Putting an initialized
C++ object directly into a `NOLOAD` section would lose those defaults.

The workspace is statically required to be trivially destructible. Repeated
startup ends the old object's lifetime and constructs a fresh one without
resource leaks, pointer rebinding or heap failure. Placement construction does
not allocate. An external-BSS selection without PSRAM boot initialization is a
compile-time error. Native configurations without external BSS keep the same
workspace in ordinary static memory; they still must pass the unchanged final
IQ proof if IQ is enabled.

## Target byte accounting

The official Xtensa compiler reports these exact sizes:

| Object | 64-record profile | 128-record profile |
|---|---:|---:|
| Sleep replay | 2,496 | 2,496 |
| Latest checkpoint | 992 | 992 |
| Performance replay | 3,672 | 5,720 |
| Complete external workspace | 7,160 | 9,208 |

Alignment is 8 bytes. There is no new internal pointer or control allocation.
The initial feature-enabled native Waveshare image reached `_heap_start` at
`0x3fcb1770`, overlapping the fixed IQ bank by 6,000 bytes. This change removes
7,160 bytes of its static internal payload; actual final margin must be measured
after all other enabled components have linked.

The IQ bank stays `[0x3fcb0000, 0x3fcc0000)` and its IRAM alias stays
`[0x403a0000, 0x403b0000)`. `scripts/radio_iq_proof.py` is unchanged and remains the
mandatory final-ELF boundary. Object checks cannot substitute for its section,
load-segment, reservation, static-end and exact-ROM-entry checks.

## Reproduction and qualification limits

Run the native host construction/custody regression:

```sh
IDF_PATH=/path/to/esp-idf bash test/run_native_diagnostic_workspace_test.sh
IDF_PATH=/path/to/esp-idf SANITIZE=1 bash test/run_native_diagnostic_workspace_test.sh
```

It tests sleep-only, performance-only and combined selection at 64/128 records;
poisons the workspace before startup; checks nonzero defaults, full checkpoint
payloads, immutable snapshots, exact partial replay, mutual command exclusion,
owner/ISR rejection, independent RTC retention and repeated construction.

Run the bounded target-object check against an already configured native build:

```sh
python test/native_diagnostic_workspace_target_test.py \
  --compile-commands idf/build/compile_commands.json \
  --idf /path/to/esp-idf --output /path/to/separate/evidence
```

It compiles serially outside the firmware build directory; checks exact target
sizes/alignment, zero-only external BSS, internal owner/recovery/fence placement,
the unchanged RTC journal, disabled-feature omission, no allocator imports, no
global constructor, and rejection without PSRAM boot initialization.

The existing checkpoint, performance-capacity, native IDF6 USB recovery,
journal/pinned HWCDC, native observer, diagnostic source binding and stage-log
regressions also apply. These checks establish source behavior and compiler
placement only. The parent product build must independently pass the unchanged
final linked proof. No device execution, flash, radio capture, PSRAM reliability
or hardware sleep qualification is claimed.
