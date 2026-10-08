# Optional native diagnostic observation

Runtime 0.1.64 adds a default-off `RISC_NATIVE_DIAGNOSTIC_OBSERVER=1` composition option. When selected, the existing diagnostic owner calls the optional weak native symbol `risc_native_diagnostic_observer(const char*)` once per accepted plain log statement, before USB availability, queue capacity or optional replay can suppress delivery.

The observer borrows a null-terminated line only for the call. It must be bounded, nonblocking, allocation-free and must not call Runtime, providers or diagnostics. The existing owner/reentry guards reject foreign-task, null and recursive log submissions before observation. An absent hook is harmless. No app/driver ABI, import, capability, grant, native resource, persistent storage or hardware policy is added. The hook is native firmware composition only; it is not exported to loaded modules.

Ordinary builds leave the option off. Their preprocessed diagnostic implementation is identical to the parent. Existing automatic timestamp lines, best-effort USB, loss reporting and sleep recovery continue unchanged.

The X4 platform uses its own retained boot record to copy only named boot/provider/app milestones and the first completed display frame. Runtime does not own that record, select those events, write flash/NVS or infer why hardware failed. Retained evidence cannot prove survival across battery removal, brownout or external reset.

Validation: production diagnostic source with present/absent/disabled observer, disconnected USB/backpressure, owner checks, reentry and unchanged connected output; existing four-mode stage diagnostics; normal and ASan/UBSan. Product composition validates its own target call linkage and retained memory placement. No device qualification is implied.
