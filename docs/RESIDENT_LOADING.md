# Optional resident-host loading handoff

Runtime 0.1.98 appends `loading(context, relative_path)` to
`risc_resident_callbacks_v1`. UI assets, names, icons and presentation are owned
by the resident application; Runtime remains headless and supplies no progress
percentage. The unchanged callback prefix ends at
`RISC_RESIDENT_CALLBACKS_V1_SIZE`; the complete optional function pointer ends
at `RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE`. Registration accepts the old
prefix, ignores incomplete suffixes, and copies only complete known fields.
Null callbacks and profiles without resident selection perform no loading UI.
Existing client, request, result, descriptor and hardware ABI layouts remain
unchanged.

The callback runs with the live host invocation's grants and allocation ledger,
after prior child fini, grant/stream cleanup, memory cleanup and unload, and
before the next ELF mapping or init. It is invoked for direct foreground loads,
foreground chains, file-open receivers and fresh caller returns, and continuations
resumed by a newly loaded host after legacy execution. An explicitly admitted
legacy target receives the callback before the host returns for its destructive
handoff. There is no callback during legacy execution or between legacy-only
handoffs because no host is resident then.

The path is a NUL-terminated copied boot-store-relative string, at most 192
bytes, borrowed for the call. It is not a manifest name, SD data filename or
untrusted executable discovery instruction. Runtime validates and copies the
launch path before invoking the host. A callback cannot recursively launch,
register another shell, request foreground exit, or authorize terminal sleep.

- `OK`: the host completed and settled its loading frame. The next ELF may load.
  The physical loading image can remain until the child completes its first
  frame; Runtime does not manufacture a first-frame completion event.
- `BUSY`: a clean refusal before committing UI state. No target mapping/init or
  failure callback occurs. Pending chain/file-open state is discarded, including
  a resumed continuation. The host can safely handle the refusal and accept a
  later independent request.
- `RETAINED`, any unknown status, or a failed post-callback native/provider
  barrier: both invocation authorities are fenced, with no next load, failure
  callback, host fini or provider cleanup retry. Images, memory and resources
  remain pinned as required by the existing retention contract.

Admission failures never trigger loading. A clean load/init failure after a
successful loading callback follows the existing failed-child notification and
file-open return behavior. The notification runs only after cleanup; retention
never enters failure UI.

## Verification

`bash test/run_resident_loading_test.sh` uses production Runtime and Graph with
real host shared-object mappings. It checks loading-before-map and old-child
unmap-before-loading, exact paths, host authority, recursive/foreign denial,
direct and chained loading, file-open success and failure returns, destructive
legacy handoffs, fresh-host resume, clean BUSY cancellation (including resumed
file-open receivers and caller returns), callback/native/
provider retention, and old/partial/null callback registrations.

`RESIDENT_NATIVE_MEMORY=1 SANITIZE=1 bash test/run_resident_loading_test.sh`
adds the production native allocation ledger and ASan/UBSan. The old resident
shell, legacy and policy suites and ordinary Runtime suite cover flag-off and
prefix compatibility. The new fixture is independent of any product launch
investigation. Rendering and first-frame presentation are qualified separately
by the host application owner; host tests and target builds do not qualify
hardware. No product files, device action or publication is included.
