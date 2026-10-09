# Optional native diagnostic observation

Runtime 0.1.64 adds a default-off `RISC_NATIVE_DIAGNOSTIC_OBSERVER=1` composition option. When selected, the existing diagnostic owner calls the optional weak native symbol `risc_native_diagnostic_observer(const char*)` once per accepted plain log statement, before USB availability, queue capacity or optional replay can suppress delivery.

The observer borrows a null-terminated line only for the call. It must be bounded, nonblocking, allocation-free and must not call Runtime, providers or diagnostics. The existing owner/reentry guards reject foreign-task, null and recursive log submissions before observation. An absent hook is harmless. The hook is native firmware composition only; it is not exported to loaded modules.

Ordinary builds leave the option off. They have no native observer, source or drain calls. Existing automatic timestamp lines, best-effort USB, loss reporting and sleep recovery continue unchanged. Selecting the option activates the diagnostic adapter even when the other diagnostic options and USB sleep recovery are off.

## Optional copied source

The same composition option supports an independent weak native hook:

```c
int32_t risc_native_diagnostic_read(uint32_t slot, char *out, uint32_t capacity,
                                   uint32_t *written, uint64_t *sequence,
                                   uint32_t *revision);
```

When this hook is present, `RiscDiagnostics::nativeSource()` returns the versioned
`risc_diagnostic_source_api_v1` table from `sdk/driver/RiscDiagnosticSourceV1.h`.
`main.cpp` registers it as the global, provider-only dependency
`platform.diagnostic-source@1`. Absent hooks return no table and add no platform.
The global allowlist accepts only version 1 with global scope, instance zero,
the complete table prefix and a read callback. Providers declare the dependency
normally; missing sources fail graph validation before module activation. An app
cannot acquire this capability, including through an explicit declaration/grant.
There is no native hook ELF import or new app authority.

The table has only `api_version`, `struct_size`, `context` and `read`. A read
requires the diagnostic owner, its exact context and no active output/read.
Slots are 0..8. The caller supplies every output; capacity is 1..1536 bytes,
including NUL, and `written` excludes NUL. Status 1 means one complete copied
record with nonzero revision. Sequence zero is valid, including records whose
sequence could not be assigned. Status 0 means absent. Status -1 means invalid
arguments, context, insufficient capacity or malformed native output. Absence
and failure clear all supplied scalar outputs and the supported output buffer;
an unsupported nonzero capacity clears only its first byte. The wrapper checks
the copied length, terminating NUL and revision before returning a record.

The source does no I/O, allocation, Runtime/provider calls or acknowledgement.
No pointer into retained/native record storage escapes. The native read hook
must copy synchronously and obey the same bounds. Reads do not consult storage
safety or lock state: a selected storage provider may copy a snapshot while
holding its own synchronization lock. This read grants no mutation or storage
authority and does not extend provider or boot-session lifetime.

## Post-output native drain

An independent weak `void risc_native_diagnostic_drain(void)` hook is called
once on every admitted owner line, after observation and after the output guard
has been released. RAII ordering covers early returns, including absent USB,
backpressure, recovery and replay suppression. The hook may therefore read the
copied source. Foreign, null and recursive lines are not admitted. Source-read
and drain reentry cannot recursively log another line. Polling does not drain.

The drain is trusted, bounded native composition work; it must not call Runtime,
providers or diagnostics, and must not recurse. Runtime supplies no storage,
path, rotation, record selection or persistence policy. A platform that supplies
this hook owns any native storage work and its lifetime/error rules. A missing
hook is harmless, and disabling the option removes all three hook call sites.

The X4 platform uses its own retained boot record to copy only named boot/provider/app milestones and the first completed display frame. Runtime does not own that record, select those events, write flash/NVS or infer why hardware failed. Retained evidence cannot prove survival across battery removal, brownout or external reset.

Validation: `test/run_native_diagnostic_observer_test.sh` compiles the production
diagnostic source with present/absent/disabled hooks, independently absent read
and drain hooks, and observer-only diagnostics. It checks disconnected USB and
backpressure drain ordering, owner/context/reentry, bounds, malformed native
outputs, copied-buffer independence, zero sequence and unchanged connected
output. `test/run_diagnostic_source_binding_test.sh` loads real host provider/app
fixtures to check dependency binding, absent/off rejection, exact global v1
registration and explicit/live app denial. Both support ASan/UBSan. Existing
four-mode stage diagnostics remain separate regression coverage. Product
composition validates its own target call linkage and retained memory placement.
No device qualification is implied.
