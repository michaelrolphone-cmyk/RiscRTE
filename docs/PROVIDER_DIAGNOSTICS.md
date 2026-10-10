# Bounded native provider diagnostics (ABI 1, Runtime 0.1.92)

This private firmware contract is selected only by a trusted compiled native
provider policy. The default `diagnosticAbi=0` refuses `printf`, `puts` and
`putchar`; `diagnosticAbi=1` permits only the exact diagnostic names present in
that policy's complete import list. Unknown ABIs and ABI 1 without a diagnostic
import reject. The full policy, including this opt-in, is copied into Runtime
and Graph ownership. The same policy reaches the repeated copied-image digest,
exact import checks, and one-shot module/task relocation scope.

The version query and compiled ABI marker must both report 1, and all three
native wrappers must be linked. Builds without the existing diagnostic adapter
report ABI 0 and their wrappers reject. Mere wrapper symbol presence is not
availability. Selected ABI-0, legacy fixed-inventory, ordinary application and
ordinary provider lookup remain denied these names; the public libc table and
application capabilities are unchanged. No raw libc or Serial fallback exists.

## Calling contract

The diagnostic sink must first be started by native firmware. Calls from any
other task, ISR, nested formatter, sink observer, or storage-drain callback
return -1 before reading caller format/string pointers. A valid call is
synchronous and nonblocking. It keeps a private reentry guard through the
existing `RiscDiagnostics::line` sink and its observer/drain callbacks.

These are trusted native pointers, not a memory sandbox. Valid contexts still
require readable memory covering the bounded input extent and varargs matching
the supported format. Task creation and arbitrary native memory access do not
become safe simply because a provider passed the import check.

There is no allocation, deferred fragment buffer, retry queue or global libc
hook. Each `printf` or `puts` call emits one completed diagnostic record.
`putchar` emits one record for that byte; consecutive characters do not assemble
a later line, so fragments from separate providers cannot be concatenated.

- Format: a NUL must occur within 256 bytes; at most 32 conversions, including `%%`.
- Strings: read at most 256 bytes, or the explicit precision (at most 64).
  A null `%s` becomes `(null)`; null format/`puts` pointers reject.
- Output: at most 255 bytes plus a private NUL. Excess replaces the last three
  retained bytes with `...`. Formatting work continues within the same input,
  conversion and field bounds; no unbounded required-length count is computed.
- Width and precision: at most 64. Dynamic fields must be within -64..64;
  negative width selects left alignment and negative precision is omitted.
- Supported conversions: `d i u o x X` with `hh h l ll z t j`; `c s p` and `%%`.
  Integer flags `-+ #0` and normal integer padding apply. Pointers are lowercase
  `0x` hexadecimal, including `0x0`, with `-/#/0`, width and precision supported.
  Strings support `-`, width and precision; characters support `-` and width.
- Floats, `%n`, positional arguments and unsupported/malformed formats reject
  the complete call before emission. Full format validation and dynamic-field
  preflight happen before string argument dereferencing.
- A single final LF or CRLF is removed; interior CR/LF/TAB becomes a space.
  Other ASCII controls, including embedded NUL and DEL, become `?`; non-ASCII
  bytes are preserved. `putchar('\n')` emits an empty record, CR/TAB a space,
  and other controls `?`. There is no multiline splitting.

`printf` and `puts` return the accepted rendered byte count, 0..255, excluding
the sink's newline. `putchar` returns its unsigned-char input on acceptance.
Rejection returns -1. These are explicitly lossy diagnostic returns, not ISO
libc's count or delivery guarantee. Transport backpressure, disconnected USB,
serial suppression, absent RAM observer or later journal eviction do not change
an accepted return. No accepted return promises persistence or host delivery.

## Existing RAM and PHY fences

The wrapper uses only `RiscDiagnostics::line`. Its existing native RAM observer
and optional journal run before the existing USB PHY fence. While the PHY is
held, bounded records therefore remain visible to the RAM observer while live
serial output and the native storage/SD drain remain suppressed. Backpressure
likewise preserves observer acceptance without waiting or retrying payloads.
The separately stopped USB exit/poll/recovery path is unchanged by this work.

## Reproducible frozen-controller policy input

`scripts/generate_usb_controller_policy.py` requires Python with `pyelftools`
and the exact frozen Reader ELF above. It checks the full SHA-256 before reading
its two import tables and binary provider descriptor. The descriptor must name
`usb-controller-esp32s3`, `usb.controller@1`, driver ABI 2 and its original
40-byte diagnostic extension. It emits the pinned version 0.1.24, both exact
requirements (`board.power.vbus@1`, `platform.usb.phy.resource@1`), complete
sorted imports, image length/hash, OS/CPU ABI 1 and diagnostic ABI 1.

The relative executable path must be explicitly supplied. It is the future
build's exact expected selected path, not a guessed product path. For a
`driver.elf` test selection:

```sh
python scripts/generate_usb_controller_policy.py --controller /path/to/frozen/driver.elf --relative-elf-path driver.elf --output /path/to/controller-policy.h
python scripts/generate_usb_controller_policy.py --controller /path/to/frozen/driver.elf --relative-elf-path driver.elf --check /path/to/controller-policy.h
```

Generation and checking never change firmware selection. Only a trusted build
explicitly setting `RISC_NATIVE_PROVIDER_POLICY_HEADER` can include the generated
header. Manifests, sidecars and boot JSON are not generator inputs and cannot
select that header. Stock and paired targets continue to omit that flag. A
changed controller or header fails the deterministic check. The host composition
runner can receive `CONTROLLER_ELF` and `CONTROLLER_POLICY_HEADER` to exercise
that exact generated selection without changing either target or running the
controller's target instructions.

## Qualification and limits

Focused tests cover bounded formats, integer extrema, star limits, null and
unterminated input, truncation, controls, fragment semantics, rejected-context
bad pointers, disabled builds, reentry and lossy returns. The sink suite composes
the actual pinned HWCDC implementation and current diagnostic sink across
stage/journal profiles to check owner/ISR/observer/drain rejection, backpressure
and held-PHY RAM capture without serial/SD work.

The composition suite uses the real trusted executor, GraphV2, ESP_PLATFORM
ModuleV2, SHA-256, ELF validation/import matching, loader/resolver, formatter and
sink together. Only target primitives, architecture relocations and final
Xtensa entry are simulated. It covers policy snapshot/graph copying, ordinary
and legacy denial, unknown ABI, exact selected addresses, nested and
foreign-task isolation, module failure cleanup and accepted diagnostic calls.

The separate real-loader availability matrix covers absent/disabled adapters,
missing wrappers and mismatched marker/query values.

With `CONTROLLER_ELF` set, the same suite additionally pins the actual Reader
0.1.24 controller SHA-256
`d0864938e573ac933fefc106991cfb033ec9599cb09902a518c53ffcaf57926c`, checks all 47
imports across both symbol tables, uses test-only policy metadata matching its
manifest identity and two dependency requirements, and simulates relocation under explicit
ABI 1. No target controller code is executed. The target audit separately reads
actual linked public/private tables, the compiled diagnostic ABI marker and
wrapper addresses; it does not infer availability from symbol names alone.

```sh
bash test/run_provider_diagnostics_test.sh
bash test/run_provider_diagnostics_sink_test.sh
bash test/run_provider_diagnostic_availability_test.sh
bash test/run_provider_diagnostic_composition_test.sh
SANITIZE=1 bash test/run_provider_diagnostics_test.sh
SANITIZE=1 bash test/run_provider_diagnostics_sink_test.sh
SANITIZE=1 bash test/run_provider_diagnostic_availability_test.sh
SANITIZE=1 bash test/run_provider_diagnostic_composition_test.sh
pio run -e esp32s3 -e esp32s3-16mb-paired -j 1
```

On the current executor sanitized runs use `ASAN_OPTIONS=detect_leaks=0` because
ptrace prevents LeakSanitizer. ASan and UBSan remain enabled. Exact clean source,
commands, artifacts, hashes and results are recorded in the local receipt.

The stock target has no enabled diagnostic adapter and reports ABI 0. The paired
target sets `ARDUINO_USB_CDC_ON_BOOT=1` with the board's `ARDUINO_USB_MODE=1`;
that enables the existing HWCDC recovery/retained diagnostic sink and ABI 1.
The four-byte const marker is checked in its readonly input-object section and
its final ESP32-S3 DROM load placement in both ELF and firmware binary,
together with all wrapper addresses. The
vendor merged `.flash.rodata` output retains ALLOC|WRITE ELF flags; the audit
records these and checks the linker memory region's `r` attributes rather than
inferring hardware write protection from that flag. The real-loader matrix
checks marker/query consistency.

Default stock/paired targets still select an empty provider policy set and do
not enable the native USB PHY resource. Closing these three import gaps does
not establish the exact product selection, board VBUS root, USB protocol,
physical controller admission or hardware qualification. No physical operation,
product activation or source publication is part of this unit. Publication
remains blocked by the separately stopped .82 ancestor.
