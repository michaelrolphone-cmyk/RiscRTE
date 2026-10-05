# Profile-driven provisioning: bounded core checkpoint (0.1.17)

This checkpoint adds a private, CPU-neutral profile parser and boot coordinator.
It is **not called by `setup()`**, does not register an ELF capability, and does
not write flash, select partitions, access a device, or change the current default
launch. No Watch content, board pins, or product endpoint is compiled in.

## Existing mechanisms and why there is no unsafe shortcut

Runtime already admits a complete board/driver/app-policy graph before starting
any provider. Station/TLS primitives and the immutable paired-bank transaction
supply pieces of the eventual native adapter. The current app-update operation
intentionally permits only replacement of one existing app with unchanged
identity/grants. It forbids changes to drivers, board, inventory, or boot policy.
A provisioning profile must therefore **not** be passed through that operation
by relaxing its validation. The paired update/rollback contract in PAIRED_BANKS.md
and installed-default lifecycle remain unchanged.

## Owner-supplied profile format

The schema is `riscrte.provisioning`, version 1. Its exact fields are:

- `schema`, `schema_version`
- `wifi`: `ssid` (1–32 UTF-8 bytes) and `password` (empty for an open network,
  otherwise 8–63 UTF-8 bytes). No actual credentials belong in a repository.
- `files`: 3–32 objects, each containing `path`, `url`, `bytes`, and `sha256`

Each path is a strict relative Runtime path of at most 192 bytes. `boot.json`,
`board.json`, and `default.elf` are required. The downloaded boot manifest selects
its actual board and default; complete-store validation must establish every
referenced manifest/module exists and is admitted. Other application/driver
manifests and ELFs are explicit file entries; there is no recursive resolver.
Duplicate paths and file/directory prefix collisions are rejected. Each file is
1 byte–8 MiB; the entire profile is at most 16 MiB. These are parser bounds, **not**
storage-capacity promises. The eventual backend must apply its stricter available
capacity before writing.

SHA256 is exactly 64 lowercase hexadecimal characters. Sources must be HTTPS
URLs with an ASCII DNS hostname and strict Runtime-style path. Schema 1 excludes
ports, userinfo, query strings, fragments, percent escapes and signed URLs. The
transport must verify TLS, forbid redirects and treat URLs only as download
sources. A hash pins content; it does not authenticate who authorized a profile.
The supplied profile is a trusted owner's local boot configuration, never an
untrusted remote instruction or authority granted to an app.

The existing strict JSON gate rejects duplicate keys, unknown fields, invalid
UTF-8, coerced types and excess nesting/size. Output is cleared on parse failure
and destruction. The parser's JSON allocation copies are wiped on free/shrink.
The caller must also clear its original profile input. Neither the parser nor
coordinator logs URLs, Wi-Fi data or profile content.

## Coordinator contract

`Coordinator` is a bounded, cooperative, boot-owner-only state machine. It runs
before drivers or applications. The caller allocates the profile off the native
small task stack and keeps it and the backend alive until a terminal outcome.
The trusted caller computes SHA256 over **all exact profile bytes**, including
credentials, and passes that digest separately; it never accepts a profile field
as its own checksum. Whitespace changes also change this identity.

1. Recover abandoned private staging before any comparison/download.
2. If a known committed installed store has the same profile digest, use it
   offline without connecting.
3. Connect, open empty private staging, and cooperatively download each entry
   with at most 4096 payload bytes per callback.
4. Validate the entire staged inventory, readback lengths/hashes, board graph,
   manifests, native ELF structure/imports and capacity without executing code.
5. Close network/files before making a final store selection.
6. Atomically select store plus profile digest while retaining the old installed
   store for explicit app-health confirmation/rollback. Return `Restart` only
   after known successful selection and a still-safe native state.

Backend callbacks must be bounded, owner-gated, and must not retain app pointers.
Pending callbacks resume across `step()` calls. The overall deadline is 300 s;
cleanup has a separate 30 s deadline, using wrapping unsigned time arithmetic.
These deadlines detect an overdue synchronous call after it returns; they cannot
preempt a stuck SDK operation. Caller scheduling/yielding is still required.

Pre-selection failures clean up before returning `Installed` (known intact
installed default) or `Recovery` (no usable installed default, remain idle for
retry on a later boot). Failed/pending-overdue cleanup or unsafe native ownership
returns `Retained`; it cannot run fallback, unload resources or force a reset.
An ambiguous selector write returns terminal `SelectionUnknown`, forbidding
abort, rewrite and automatic retry. Only a separately safe reset or read-only
reconciliation may resolve it. The coordinator itself never reboots or launches.

## Verification and limits

`bash test/run_provisioning_test.sh` and its `SANITIZE=1` variant compile the
production parser/coordinator with the real Runtime, graph and a host dynamic
`default.elf`. A filesystem-backed model streams bytes from a fake HTTPS source,
checks readback hashes and inventories, runs real full-graph admission, models
selection/reset, and executes the installed default. Fault tests cover malformed
profiles, offline first boot/update, unchanged profile, each preselection phase,
corruption, hash-correct invalid graph, interruption/retry, native retention,
cleanup deadlines, time wrap, uncertain selection, and fallback launch.

The model is not a native flash implementation or TLS test. Its host dynamic
module does not qualify Xtensa ELF admission. Its directory renames model a
commit boundary but are not an atomic power-loss-safe deployment protocol.
Native SPIFFS/SD capacity, durable selector/digest storage, full ELF readback,
transport deadlines, whole-store staging, fresh-boot routing and boot-health
rollback remain to be connected and tested. Existing paired-bank and update
regressions continue to run. Host success does not qualify a device.

## Remaining integration

A native whole-store adapter must reuse the paired-bank safety ordering without
weakening existing app-update authority. It must stage new board/driver/app
inventory safely, preflight complete-store capacity, retain/recover both store
and profile identity across cuts, validate graph/imports before selection, and
persist no plaintext credentials outside the explicitly supplied profile.
Existing fixed layouts must fail closed when this cannot be done; no automatic
partition resizing, filesystem formatting, migration or active-store mutation
is introduced. An SD path additionally needs independently bootstrappable,
noncyclic storage/controller ownership. The installed firmware's `setup()` must
only invoke provisioning once those backend invariants are actually implemented.
