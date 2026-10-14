# Interrupted native import verification, 2026-10-12

Base: `18c4771bccdec307f39163111477a0ea83f6724c`.
All runs use the isolated Runtime worktree; no product selection, publication,
flash, device access, or original-source deletion.

- `import-sanitized.txt`: combined AddressSanitizer/UndefinedBehaviorSanitizer;
  original-byte binding and preservation, exact prefix/candidate refusal,
  callback custody, every returned-error and allocation-failure recovery.
- `power-cut-sanitized.txt`: combined ASan/UBSan `fork`/`_exit` process-death and
  new-object reboot recovery, including incomplete AppData writes and original
  absent/empty native metadata namespaces. NVS is a deterministic atomic-key
  host model, not real flash.
- `artifacts-sanitized.txt`: combined ASan/UBSan canonical Fresh/imported artifact
  inspector, exact-prefix, malformed framing, alias and I/O/custody tests.
- `regressions.txt`, `baseline-regression.txt`: original checked legacy/Fresh,
  core/store, baseline admission and first-SD creation suites.
- `sd-attempt-regression.txt`, `sd-consumers-regression.txt`: actual X4/FatFs
  copied-worker and typed-consumer compositions against pinned source inputs.
- `target-compile.txt`: disconnected full import Core/admission/store/collector
  entry points against real Xtensa ESP32-S3/NVS headers, GCC 8.4.0.
- `artifacts-target-compile.txt`: identical normal/ESP unsupported-target objects,
  with no filesystem imports. Target qualification remains blocked.

Sanitizer commands use `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1
SANITIZERS=address,undefined`. LeakSanitizer cannot operate in this executor's
ptrace environment. Explicit handle, iterator and allocation accounting remains
active. Source allocator tracking also includes AppDataFiles temporary buffers.

Pinned cross-repository inputs:

- System Apps: `c564dd665072db141fc42d55be3be52511750719`
- X4: `c8004c5c8362b142ebb35a0e1c5d266532a11581`
- Drivers: `281609a0d2ae514f107d2ef67b6e8b4d25c72fb8`
- Reader: `45cf61ac013fb618e8d7fed63217a35350484d52`

Resource limits and integration obligations are in
`experimental/native_replica/IMPORTED_INITIALIZATION_RECOVERY.md`.
