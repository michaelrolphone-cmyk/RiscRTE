# Profile-driven provisioning: bounded core checkpoint (0.1.20)

This checkpoint adds a private, CPU-neutral profile parser and boot coordinator.
It is **not called by `setup()`** and does not register an ELF capability or change
current default launch. Its private native staging API writes only the verified
inactive paired bank when a compiled-in boot owner calls it. The default boot
path does not call it. No Watch content, board pins or product endpoint is
compiled in; no device has been accessed to test this change.

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
transport must verify TLS, retain bounded HTTPS-only redirects and treat URLs only as download
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
The native paired-store implementation below adds bounded inactive staging and
committed identity. Transport, fresh-boot routing
and boot-health integration still remain. SD capacity and behavior are untested. Existing paired-bank and update
regressions continue to run. Host success does not qualify a device.

## Remaining integration

Complete boot integration must preserve the paired-bank safety ordering and
existing app-update authority. The native file backend below stages inventory
and committed profile identity; production admission now connects
complete graph/import checks before selection, as described in 0.1.20 below. No plaintext credentials are
persisted outside the explicitly supplied profile.
Existing fixed layouts must fail closed when this cannot be done; no automatic
partition resizing, filesystem formatting, migration or active-store mutation
is introduced. An SD path additionally needs independently bootstrappable,
noncyclic storage/controller ownership. The installed firmware's `setup()` must
only invoke provisioning once those backend invariants are actually implemented.

## Private paired-store transaction prerequisite (0.1.18)

`RiscUpdate::Transaction::beginStore` now reuses the existing paired-bank
invalidation, firmware clone, store clone and clone-readback sequence. It only
opens a private staging callback after that cloned store passes its active
digest check. The ordinary provider table and SDK layout are unchanged; neither
`begin_app`, `begin_firmware` nor their `write` calls can reach this authority.
The new optional backend hooks are supplied by the private native staging API
in 0.1.19, described below.

A boot-owned backend may write its bounded staged inventory only while
`stagingStore(token)` is true. This rejects stale tokens and expired operations.
`finishStore` requires the backend's complete inventory/hash/graph/ELF validation
and returns the expected full-partition store digest. Existing paired firmware
and store readback then run again; any changed store byte or firmware clone
fails before readiness. Cleanup, readiness journal and ambiguous OTA selection
retain their existing ordering. A failed staging transaction can be aborted and
followed by an ordinary app update without leaking its broader mode.

The paired model additionally checks missing hooks, stale active digest/token,
pre-write cloned-store corruption, whole-store validation refusal, retained
cleanup, deadline expiry, late store/firmware corruption, successful activation
and ambiguous selection. Normal and ASan/UBSan runs pass. This makes a native
whole-store backend possible without misusing app-update policy; it does not
implement that backend, network transport, or boot routing by itself.

## Native inactive-store backend (0.1.19)

`NativeBankStore` now offers a separate compiled-in provisioning API before
Runtime platform binding/application startup. It requires the existing verified
16 MiB paired layout and a confirmed current image; a pending unconfirmed boot
cannot begin another provisioning transaction. There is no SDK/provider-table
expansion and no changes to partition layout, active store or active firmware.

`provisionBegin` preflights path/name bounds and conservatively limits file payloads
(including profile identity) to 75% of the fixed store capacity. Real SPIFFS
allocation/write failures still fail closed. It allocates staging metadata from
PSRAM, then reuses the existing transaction's inactive clone and readback. Only
a successfully verified clone is mounted at `/updatefs`, with formatting disabled.
The private file backend removes only that staged inventory, streams each file
with exact length/SHA verification, independently rereads every file, checks the
complete inventory and performs compiled-in graph/ELF admission. In 0.1.20 this is the mandatory
production implementation below; callers can no longer provide an admission
callback that bypasses it.

The profile's 32-byte SHA is saved as `.provision-sha256` inside the candidate
store, read back, and all payloads are checked again after that metadata write.
This makes the identity part of the same pair's existing full-store SHA/journal;
there is no separate mutable profile selector. The backend closes/unmounts the
filesystem, computes its raw partition digest, and the paired transaction checks
another full raw readback before readiness. Native cleanup, owner/resource safety,
stale token, deadline, selection ambiguity and safe restart checks remain required.
Abort success releases the private stage and permits the intact installed boot.
Failed cleanup retains ownership and prevents boot handoff.

Host file tests use the production `StoreFiles` implementation and real Runtime
graph preparation. Actual `NativeBankStore` source is also tested with the pinned
bootloader bytes, fake IDF flash and a deterministic filesystem-to-flash model.
The tests cover success, abort/fallback, late corruption and ambiguous selection,
and verify old firmware/store digests are unchanged. Both suites pass normally
and with ASan/UBSan. The model is not SPIFFS power-loss emulation or hardware
qualification. Native compiled target CI must also pass before using the change.

Still remaining: mapping the
coordinator to the native stage and existing station/HTTPS transport, a genuinely
current bootstrap UTC source, profile acquisition outside the immutable installed
store, and `setup()` integration. Clock-unavailable boot must keep the installed
default/offline recovery path; a persisted timestamp is not automatically current.
The native HTTP implementation's certificate-validity checks are unchanged.

## Mandatory production admission (0.1.20)

`provisionBegin` accepts copied native hardware tables and the real boot owner's
immutable KV backend, not a caller-supplied admission function. The native file
backend now allocates a fresh CPU port and Runtime in PSRAM for candidate
validation. It applies the same reserved pins as normal boot through a shared
helper, binds inert native CPU/bank/KV capability metadata, and prepares the
complete staged board, provider dependency graph and app-grant policy. It does
not replace the running-runtime singleton or mutate the normal CPU port.

The internal Runtime image inspector is available only after successful prepare
and before execution. It enumerates the exact selected default, declared app
images and driver images, preserving application/driver role. Repeated driver
instances inspect their shared file once; a default app's matching policy is
deduplicated. Every required image must be present in the pinned profile and
read back to its exact size/SHA. Additional `.elf` files are also inspected, so
an unselected corrupt child/provider cannot hide outside the prepared graph.

Native admission uses the existing structural Xtensa ELF validator, ordinary
import allowlist and actual native symbol registry. App images require a global
function `app_main` and a paired optional init/fini; selected providers require
a global function `t5_driver_get`. Extra ELF files must satisfy at least one
supported entry role. Each ELF remains capped at the existing 2 MiB native-update
limit; profile parser limits do not override it. Image buffers, CPU metadata and
Runtime metadata use checked PSRAM allocation with no internal-RAM fallback.
Owner/resource-safety and observed time bounds apply throughout reads/scans.

No ELF is mapped, relocated or executed during this pass. In particular, the
returned provider ABI table, provider initialization, driver/hardware behavior,
actual app health and physical rollback are not qualified by static admission.
Those retain the normal startup/health boundary after a selected pair reboots.
A read-file `fclose` failure latches uncertain ownership: metadata is safely
discarded (no providers were started), but native staging remains blocked; abort,
fallback binding and restart cannot claim clean closure or retry that descriptor.

The native test executes 18 graph/image scenarios: valid selected driver and KV
policy, missing required modules/default, incorrect entry roles/architecture,
forbidden imports including a static-symbol-table import, missing native symbol,
reserved pins, absent CPU/KV support, invalid extra ELF, and three PSRAM allocation
failure cuts. All reject before readiness and retain the installed pair. Accepted
cases produce zero provider/peripheral calls. A separate close-failure injection
proves retained cleanup, no double-close, no fallback binding and no forced reset.
Ordinary updater regressions and pre-/post-execution image-inspector guards remain
in the tests. Normal and ASan/UBSan runs pass; local LSan is unavailable under
ptrace. Exact-head target CI is required before this checkpoint is considered
software-verified.
