# Component freeze: native serial lifetime repair

Base Runtime: `615bfaeeb9f8876255380994a95bebfafaa32779`.
Failing regression checkpoint: `20d2dcb1be398f69f0a668677737fbf43f1c5d35`.
The source and evidence are isolated in `/workspace/shared/serial-lifetime-20261018`.
Original Runtime, provider, Wave producer and Watch source checkouts remain clean.

## Fixed behavior

- Run lifetime remains the default. Shared native SD binding explicitly opts in
  to firmware lifetime and requires both session reset and final endpoint stop.
- Clean return resets old session/Settings, releases app/FileSource/ordinary boot
  grants and validates exactly the retained slot/generation pairs, their actual
  acquired dependency closure and exact consumer counts. Empty keep sets retain
  ordinary shutdown. Unrelated modules unload by normal recursive release.
- The actual outer-loop helper checks current Runtime custody/graph/readiness on
  every checkpoint. Healthy open serial handles remain valid between requests.
- Final stop disables endpoint admission before retiring native clients and using
  normal graph shutdown. Uncertain custody remains retained. Repeat run, late
  binding and post-return installed-app admission are rejected.
- Runtime-bound callback closures cannot be admitted for firmware retention.
  Boot-health confirmation and paired rollback behavior are unchanged.

## Passing evidence

1. Released Wave 0.2.2 plus independent real pinned LittleFS namespace 2: 11 cases
   each normal/ASan+UBSan; complete create/guarded replace/COMMIT/CRC readback/CLOSE,
   absent SD/private usability, stale SID/PATH, failed close/release, default run,
   repeated sessions/run rejection and final stop. Actual Python client default
   5000 ms deadline completes 103 requests/configuration. See parent README and
   `wave-*.json`, `wave-*.log` for exact commands, hashes and TX/RX.
2. Synthetic shared product startup: 13 cases per Wave12/X4SD9 mapping,
   normal/sanitized. See `product-*`.
3. Read-only exact graph keep-set: all four capacity branches, normal/sanitized,
   plus existing graph/dependency cleanup regressions. Invalid/stale/duplicate/
   pending grants, unsafe lifecycle/leases/streams, leaked roots/consumers,
   actual image-source edges, diamonds and byte-stable validation covered. See
   `graph/receipt.json`, `graph/commands.txt` and raw logs.
4. Firmware admission closure exclusions, required callbacks, default Run,
   temporary readiness/control-only dispatch, real parked grant/consumer leaks
   with an open handle and undrained old-session rejection: six modes pass
   normal/sanitized. See `lifetime-guards/receipt.txt` and raw logs.
5. Default feature-off Runtime and existing explicit default-Run native suite
   rerun normally/sanitized after final source hardening. Other supplemental
   host checks and their timing/limitations are listed in `supplemental/README`.
6. Scoped Xtensa object/layout checks with verified compiler and boundary shims:
   all five modified production TUs compile feature on/off, Runtime and binding
   array size unchanged at all four capacities, Port grows 4 bytes. Maximum
   validator own static frame is 336 bytes, not a whole-call stack bound. See
   `target/README.md` and machine-readable target receipts.

The focused serial-files workflow now runs the graph and admission controls,
released Wave 0.2.2 post-return test and actual client traces. Its native compile
list includes ProviderGraphV2. No held hardware-fixture workflow was changed.

## Explicit limits

No device, serial, flash or physical fixture was accessed. The physical .59
cause remains unresolved. ASan/UBSan passed with LeakSanitizer disabled after a
verified ptrace fatal error, whose raw logs are preserved. Full current-source
SDK link/static IQ-bank overlap remains unqualified because the full SDK/link
inputs are absent. Scoped target shim compiles are not that proof. Actual X4
provider host qualification was not run locally because pinned inputs are
absent; the updated existing focused CI step retains its exact pinned sources.
The installed-discovery runner's preexisting GCC indentation warning and a
warning-only workaround are recorded separately. No provider/FatFs, Watch app,
wire protocol, timeout or formatting behavior was changed.

`freeze-index.json` hashes the committed evidence and lists all changed source,
test, CI and documentation files. Subordinate receipts retain their actual
pre-commit HEAD and paths; source hashes identify the tested candidate.
