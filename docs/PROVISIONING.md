# Profile-driven provisioning (Runtime 0.1.36)

## Current first-install workflow

[First-install packaging](FIRST_INSTALL.md) is the current entry point. A verified
public Runtime seed plus a private owner profile can be assembled offline into
one new-device flash image. On boot, Runtime's built-in flash/NVS, ESP32 station,
SNTP and verified HTTPS primitives fetch the complete owner-pinned inventory,
including `board.json`, driver manifests/ELFs, app manifests/ELFs and
`default.elf`. Network bootstrap does not depend on downloading its own Wi-Fi or
storage driver. Product providers start only after full board/graph/ELF admission
and selection of a verified inactive pair.

Current source preserves main's app-data and full-cohort update contracts and
Runtime 0.1.35's optional IQ resource/sleep diagnostics. ABI1 and ABI2 remain
separate explicit layouts; current Watch needs `esp32s3-16mb-appdata-iq`. Its
profile is generated from a complete verified product store and immutable
published file URLs, not an app-only catalog or inferred dependency list.
A provided `cohort.json` must match the exact cloned Runtime version, firmware
length/hash and selected layout. App-data and unrelated NVS contents are never
provisioning update targets. Successful profile consumption is also carried in
the existing paired-journal trailer across ordinary app, firmware and full-cohort
updates. A confirmed newer product therefore remains offline on the old unchanged
owner profile, even when its immutable cohort image has no private digest file.
Corrupt/nonempty receipts fail closed; pending boots never consume them as health
confirmation. Receipt write/readback must succeed before readiness/selection.

Metadata close failures now propagate through board/boot/driver/app/cohort reads.
They retain native ownership, block admission/cleanup/restart and keep a live
invocation mapped. No fallback is launched after a failed close.

Schema 1 remains supported. Compact schema 2 has exact root fields `schema`,
`schema_version`, `wifi`, `base_url`, `files`. The base is a canonical HTTPS
directory URL ending in `/`; each file has exactly `path`, `bytes`, `sha256`.
Its URL is the base plus the strict relative path. Both schemas allow 3–128
files; the owner-input blob remains bounded at 16 KiB. `boot.json`, `board.json`
and `default.elf` are required. Unknown fields, mixed URL forms, duplicate paths,
escaped/query/userinfo URLs and bad UTF-8 remain refused. Public examples contain
no credentials; private owner outputs must remain local and outside Git/artifact
upload paths.

Native capacity now uses pinned SPIFFS page/index/write-amplification accounting,
including the profile digest and four reserved blocks, rather than the earlier
75% payload approximation. Incoming chunks remain at most 4096 bytes; an 8192-byte
PSRAM buffer coalesces short HTTP reads into unbuffered filesystem writes.
Readback checkpoints remain at most 4096 bytes. The pinned SPIFFS host proof
covers a real 83-file Watch store, repeated 4096/512/37/1-byte input, failed writes,
hash/readback corruption, interrupted remount/retry and admission refusal/retry.
Allocation/GC/I/O failure still preserves the active pair; the precheck is not a
promise that arbitrary damaged media can be repaired.

First-install composition is explicitly for a NEW device: its complete image
contains private NVS and, for ABI2, initial empty app-data. It must never update an
existing device. Existing devices retain the separately authorized maintenance
input route and inactive-bank updates. A full NVS partition fails without erase;
two maximum-sized profiles plus historical/other NVS keys are not guaranteed to
fit. The original layout and maintenance planner remain ABI1-only.

The generic e-ink profile machinery is ready for a complete admitted product
store. The existing X4 heartbeat is not a functioning Reader. See the
[concrete Reader product dependency plan](EINK_PROVISIONING_GAP.md).

## Historical checkpoints and underlying contracts

The following sections record the earlier implementation stages. Their old
remaining-work statements describe those checkpoints; the current workflow and
limits above take precedence.

Paired `setup()` now reads bounded owner-controlled descriptor/profile input
from existing NVS and runs the provisioning coordinator before normal Runtime
creation. Missing/unusable profile or unavailable fresh time continues through
normal installed-store validation and launch. Retained resources or selected /
ambiguous activation block normal launch. No deployment credentials, package URLs
or time-service trust roots are embedded in the firmware.

The bounded SNTP adapter now acquires a new synchronization from owner-configured
servers. Without the optional time input it remains unavailable. Deployment still
requires owner profile/time-server inputs and package HTTPS trust roots. Current
target/host checks do not qualify Wi-Fi/TLS operation or physical interrupted writes.

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

The coordinator/native-stage/station/HTTPS/setup and read-only profile-input
wiring described in 0.1.21 below is now implemented. The SNTP implementation in 0.1.25 supplies the time acquisition mechanism;
owner-provided deployment input remains required. Clock-unavailable boot must keep the installed
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

## Bounded bootstrap input and setup flow (0.1.21)

The native input adapter reads only namespace `rte_bootstrap` in the existing NVS
partition. App KV namespaces are numeric `rteXXXXXXXX` and cannot name it. The
fixed `descriptor` key is a blob of at most 256 bytes:

```json
{"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"}
```

`profile_key` names one owner-provided NVS blob, at most 16 KiB, using the profile
format above. It is not a filesystem/partition selector or a remote URL. There
is no default profile, credential, endpoint or assumed device identity. The
adapter opens NVS read-only, probes bounds, verifies the returned size, hashes
exact profile bytes and wipes owned scratch on every exit. It never calls NVS
write, commit, initialize or erase; existing startup's no-erase recovery remains.
Insufficient existing NVS capacity must be handled by the owner's provisioning
tool without erasing settings or changing the layout. This change does not add
a device-writing tool or publish any owner's credential-bearing profile.

After the selected pair/store is verified and mounted, paired setup invokes the
bootstrap coordinator before allocating/binding the normal Runtime. A pending
unconfirmed OTA image bypasses provisioning and follows its normal installed
validation/app-health confirmation. A later confirmed boot may provision. An
unchanged installed `.provision-sha256` works without network or a time source.
Missing, invalid, unavailable or oversized input preserves the installed launch
path; its normal manifests/ELFs are still validated, never implicitly trusted.

For a changed profile, the coordinator reuses the existing native station and
HTTPS API with bounded polling, exact response lengths and explicit closure.
Its fresh-time callback can return pending, unavailable, or a sample containing
UTC plus a same-boot monotonic observation and a maximum age (up to 300 s).
Future/stale observations, invalid UTC range and unavailable time are rejected.
The callback contract requires genuinely current UTC; those bounds do not
authenticate a fabricated timestamp. UTC is never accepted from the descriptor
or profile, saved across resets, inferred from firmware age, or substituted for
certificate verification. At the 0.1.21 checkpoint the default weak factory returned
no provider; 0.1.25 replaces it with the optional owner-configured SNTP adapter below.

The connected network is closed before peak-memory staged graph/ELF admission.
Success commits through the existing inactive paired-store transaction and only
then requests its safe restart. Ambiguous selection never aborts/retries. A
refused or unexpectedly returning restart keeps the session and blocks app
launch. HTTP/radio/file-close retention also blocks launch instead of falsely
claiming cleanup. Ordinary download, time, input and staging failures clean up
before continuing the normal installed default. Diagnostics contain only fixed
action/reason labels, never profile bytes, SSIDs, passwords or URLs.

The bootstrap test drives actual production coordinator/native-stage/admission
code against fake HTTPS/radio/time and fake IDF flash. Its 24 fresh-process modes
cover absent/invalid input, unchanged profile without time, unavailable/stale/
future/invalid/pending/timed-out time, partial HTTP open and download/corruption/
length failures, retained HTTP/radio/matching-file close, native safety/OOM,
activation and ambiguous selection. Fallback modes really execute the installed
host ELF. Pending OTA boot skips input/network/update, validates and confirms its
installed default normally; a separate confirmed-boot process then provisions.
NVS tests prove read-only namespace use, size/type/torn-size refusal, exact SHA,
scratch wiping and no erase recovery. Normal and ASan/UBSan checks pass; local
LSan is unavailable under ptrace. Exact-head paired target CI remains required.

## Bounded selection-attempt guard (0.1.23)

A regression reproduced a real automatic retry gap: after a new default failed
health and the bootloader returned to the old VALID pair, an unchanged desired
profile was downloaded/selected again. The old code's fresh-boot model made one
network connection, 2,535 modeled writes and another restart instead of launching
the installed default. Preserving the old pair alone did not prevent this loop.

The fix reuses each bank's existing 4 KiB journal sector. The first 96-byte
readiness record is unchanged. An optional 176-byte `RPT1` trailer at offset 96
records the profile SHA, destination firmware/store hashes, source firmware/store
hashes, bank, format and CRC. CRC is corruption/torn-write detection, **not
authenticity**. No new partition or NVS writes are added. All-FF is compatible
legacy/untracked history; a legacy attempt can be retried and becomes tracked
when selection is next attempted.

The trailer is written and read back only after READY, immediately before
calling the OTA selector. Earlier download/admission failures leave no attempt
and remain retryable. If this invocation's trailer write/readback fails, the
selector has definitely not been called: existing abort cleans its inactive
staging and the installed default can launch without reboot. A reset during that
write is different: a later boot cannot establish its history, so a nonempty
malformed/unreadable trailer declines automatic mutation and still uses the
installed default. It is not silently erased or trusted.

If this boot's installed profile differs from the desired profile, and the
inactive bank has a valid matching attempt from this exact verified source pair,
the unchanged desired profile is held **before network or flash mutation**. This
covers health rollback and ambiguous selection. A different desired profile may
proceed. A changed verified source pair also makes older history inapplicable,
so a later intentional return to an earlier successful profile is not globally
blacklisted. This is one bounded source-to-destination attempt identity, not a
lifetime blacklist or an authentication mechanism. Any owner byte-change to the
profile is a different exact profile SHA; no semantic/canonicalized retry policy
is inferred. Valid destination bank/hash binding is required before considering
source/profile identity. Malformed history stays a safe offline hold even when
its identity cannot be compared.

The previous generic read-only NVS input, installed-default and pending-bank
health paths remain unchanged. Ordinary app/firmware updates still use their
existing selector and authority; they do not receive provisioning-attempt policy.
Their existing inactive-sector invalidation naturally removes obsolete trailers.

Focused tests now cover same failed profile held repeatedly with zero network,
selector or flash calls; changed profile and changed verified source proceeding;
legacy records; torn/malformed/unknown-format/read-failed trailers; bank and both
destination hash mismatches; known pre-selector write/readback failures with
ordinary abort; and unchanged ordinary updater behavior. Existing 24 bootstrap
cases remain. All failures preserve the active firmware/store bytes and launch
the installed host ELF when resource cleanup is safe. Physical journal faults,
SPIFFS/power-loss behavior and actual app/hardware health remain unqualified.


## Bounded SNTP and offline owner artifacts (0.1.25)

The read-only `rte_bootstrap` namespace may contain a `time` blob (maximum 384
bytes): `{"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.invalid"]}`.
The example is intentionally non-resolving; choose an actual deployment server.
One to three ASCII hostname/IPv4 entries, each at most 63 bytes, are accepted.
Unknown fields, static UTC and empty server lists are rejected. No server is
embedded or selected implicitly through DHCP. Missing/invalid input leaves the
installed default available. This configuration is separate from the exact
profile digest; changing a server does not override a held failed profile.

After station connection, the existing IDF4.4 SNTP implementation starts one
bounded acquisition. It requires its newly registered completion callback,
records the same-boot monotonic observation and uses the existing UTC-range and
300-second age checks. A plausible preexisting system clock is insufficient.
Acquisition expires after 30 seconds; timeout and invalid samples preserve the
normal fallback. A static one-shot context lasts for the boot: cancelled or late
callbacks cannot revive an acquisition or access a freed provisioning session.
Existing SNTP ownership is refused, not stopped. The adapter stops after its
first sample and again through coordinator cleanup if necessary; failed stop
retains the session and blocks launch/restart. A retained HTTP close may retain
its session without further cleanup, as before.

Pinned SDK source declares the `esp_sntp_*` wrappers for owner-task use.
`esp_sntp_stop` queues work; a synchronous `esp_sntp_setservername` call drains
that TCP/IP queue before callback unregistration and disabled-state verification.
No callback registration changes occur while this owned service is running.
See [IDF4.4.7 SNTP implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/lwip/apps/sntp/sntp.c)
and [System Time](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-reference/system/system_time.html).

SNTP is **unauthenticated**. A fresh client exchange is not cryptographic proof
of current UTC: the configured server or a network attacker can supply wrong
time, affecting certificate-validity decisions. Normal HTTPS chain, hostname
and validity verification stay enabled. No Roughtime, authentication keys or
new trust service is introduced. Live network/device qualification remains open.

Build the offline packager with:

```sh
bash scripts/build_provision_input_tool.sh /tmp/provision-input
/tmp/provision-input /owner/private/profile.json /owner/private/new-bundle time.example.invalid
```

The last argument is optional and must be a real owner-selected server for actual
use. The tool validates the supplied profile with the production C++ parser,
preserves its exact bytes (including whitespace/profile identity), and writes
`profile.bin`, `descriptor.bin`, optional `time.bin`, `nvs.csv`, and a final
`COMPLETE` marker. The new output directory is owner-only; existing paths and
symlinks are refused. Failure gives fixed diagnostics and removes only its newly
created incomplete output. A killed process can leave a partial directory:
consume only complete bundles and validate their contents before deployment.

`nvs.csv` uses Espressif's namespace/data/hex2bin blob encoding, not NVS strings;
see the [NVS generator format](https://docs.espressif.com/projects/esp-idf/en/v4.4.3/esp32/api-reference/storage/nvs_partition_gen.html).
No NVS partition image, partition size, address, device connection, flash, erase,
credential generation or credential upload is performed. Existing-device input
installation must preserve all unrelated NVS entries; this CSV is not permission
to replace an existing NVS partition. Profile input and output contain plaintext
owner credentials (hex is not encryption): keep them outside Git/build artifacts,
use a private local directory, and do not upload them to CI. Host process memory
is not a secure credential vault. Tests only use dummy credentials/invalid domains.

Software tests exercise the real parser, deterministic exact-byte artifacts,
NVS CSV/blob correspondence, bounds, no-overwrite and fixed diagnostics. SNTP
shim tests cover callback success, stale status, timeout, cancellation, bad UTC,
preexisting ownership, invalid config, deferred stop, stop failure and late
callbacks. Coordinator tests add failed time-stop retention to the existing
fallback/selection suite. These are software models, not NTP packets or devices.


## Generic first-install seed composition

The provisioning updater starts from a **verified paired baseline**.
`prepareBoot()` verifies and mounts that baseline before reading owner inputs.
It does not turn wholly blank/corrupt storage into an installation. A first
installation can now compose a generic fallback seed offline, using the existing
paired candidate and image tools:

```sh
python scripts/provision_seed.py --candidate dist/esp32s3-16mb-paired \
  --store .pio/build/esp32s3-16mb-paired/spiffs.bin \
  --cc "$HOME/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc" \
  --mkspiffs "$HOME/.platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino" \
  --output dist/esp32s3-16mb-paired/seed --source-sha "$(git rev-parse HEAD)"
```

Run after the existing source-clean paired firmware candidate and
`pio run -e esp32s3-16mb-paired -t buildfs -j 1` steps. The composer rechecks candidate hashes, source/version markers,
rollback bootloader, fixed paired layout, linked TLS/rollback proof and native
ELF shape. It always uses the repository's no-bus/no-device generic board and
heartbeat fallback graph; no Watch/Reader/product driver bundle is required.
The composer invokes the existing `build_apps.py` using the supplied trusted
compiler and compares the frozen store with that newly built heartbeat ELF.
A caller-provided ELF/version marker alone is not accepted. Markers/hashes
are integrity and custody checks, not signatures or malicious-input authentication.

The frozen SPIFFS input is read once, hashed and unpacked using the official
packer. Extraction must reproduce exactly board.json, boot.json and the newly
built default.elf. Composition from those fixed inputs is deterministic. Existing
paired metadata helpers create a bank0 VALID identity bound to those exact
firmware/store bytes and initial OTA data, with an erased attempt trailer.
The output directory must not exist. `seed.json` records bounded fixed-layout
segments and hashes; `SHA256SUMS` is written last. Failed composition removes
only its newly created output. A killed process may leave incomplete output;
do not consume a bundle missing its final checksum manifest.

This is a **new-install artifact**, not an update bundle or migration action.
It assumes the existing `riscrte-paired-16m-v1` layout and blank inactive bank1.
No combined flash image, erase command, formatting, resizing or device operation
is produced. NVS is deliberately omitted, so this seed cannot overwrite owner
credentials or unrelated NVS entries. Keep private provisioning-input blobs
separate and local; safe owner installation of those blobs remains deployment
work. Existing devices continue to use the inactive-bank update mechanism.

Host tests cover candidate digest/source rejection, deterministic metadata,
no-overwrite and NVS omission. A real official SPIFFS pack/unpack test covers exact content and corrupted-content
refusal. Target CI additionally composes the seed twice from the same frozen
image and freshly built paired firmware, compares bundles, and passes its actual
graph/default ELF through production native admission. Admission does not execute
the target ELF. These checks do not qualify device deployment or power loss.

Source-to-SPIFFS byte reproducibility is not claimed. Bundled mkspiffs0.2.3
(SPIFFS commit f5e26c4e933189593a71c6b82cda381a7b21e41c) can leave the three-byte
`spiffs_page_object_ix_header._align` field uninitialized: repeated identical
inputs differed at first-index-page offset261. No bytes are normalized or ignored;
the complete frozen image is retained and hashed. See upstream
[layout](https://github.com/pellepl/spiffs/blob/f5e26c4e933189593a71c6b82cda381a7b21e41c/src/spiffs_nucleus.h)
and [creation](https://github.com/pellepl/spiffs/blob/f5e26c4e933189593a71c6b82cda381a7b21e41c/src/spiffs_nucleus.c).

## Explicit owner installation transaction (0.1.31)

The existing offline tool now has an owner-invoked simulated installation mode:

```sh
bash scripts/build_provision_input_tool.sh /tmp/provision-input
# Use a private existing directory representing a SIMULATED NVS store.
/tmp/provision-input --install-sim /owner/private/profile.json /owner/private/simulated-nvs time.example.invalid
```

This mode tests the installation transaction through a directory transport. It
never finds, opens or communicates with a device. It is not a flash command or
an existing-device serial installer. The same transaction has a compiled native
NVS adapter, `installOwnerNvs`, for an explicit trusted maintenance caller;
normal boot does not invoke it, and no app export or network endpoint is added.
The separate maintenance image and explicit serial client below provide that
software transport. Physical execution remains UNRUN.

The native adapter opens only `rte_bootstrap` in read/write mode after checking
existing NVS initialization and caller quiescence. It never initializes, erases,
formats, resizes or replaces the NVS partition. Unrelated namespaces and keys,
including existing legacy profile/time blobs, remain untouched. Full NVS or
initialization failure returns an error, with no erase recovery.

The installer validates the complete new profile and optional time input first.
It reserves `installer`, `install_p0`, `install_p1`, `install_t0`, `install_t1`
only when those reserved slots are absent, then retains an ownership marker.
An unknown marker or collision refuses installation. It alternates owned slots,
commits and reads back the complete profile/time pair, and writes the descriptor
last. Descriptor schema 2 selects the profile and time keys together; an empty
time key explicitly disables time acquisition. Legacy schema 1 stays readable.
Only runtimes supporting descriptor schema 2 can use this new installer output;
an older runtime falls back to its installed application if it cannot read it.

Preselection failures leave the old descriptor selected. A selector write,
commit or readback failure returns SelectionUnknown: reload the descriptor and
compare with the intended input before retrying. Never erase on uncertainty.
An identical selected profile/time pair is a no-write success. Old and new slots
are retained; there is no automatic deletion of unrelated data or malformed
history. Torn/malformed current descriptors refuse updates. The underlying NVS
single-key persistence semantics are modeled in tests, not hardware-qualified.
Two complete profiles plus NVS overhead must fit in the existing partition;
the 16 KiB parser bound does not promise that two maximum-sized profiles fit.

The simulator models key operations and atomic selector replacement. It does
not emulate NVS pages, flash wear or power-fail durability of its host filesystem.
Its incomplete temporary file is retained on failure rather than silently
removed on a later invocation. Keep all inputs/output private: they contain
plaintext credentials, including in hex-encoded packaging, and are not CI assets.

PR15 remains the original paired ABI1 layout. Watch's separate app-data PR22
uses `riscrte-paired-appdata-v2` and different slot/store bounds; no ABI2 image or
empty app-data image is generated or installed by this workflow. Do not mix the
seed artifacts or use a first-install image as ordinary OTA.

X4 and ESP32-CAM hardware are unavailable for this task. Their checks are UNRUN,
not passed, and PR15's hardware-status workflow reports that explicitly without
polling them. All host, sanitizer, target-build and artifact checks remain in the
software integration workflow; hardware qualification is not its prerequisite.


### Explicit maintenance image and serial command

Build `esp32s3-16mb-maintenance` only when deliberately preparing owner
maintenance. This separate image enters its bounded serial command loop before
paired boot, providers or app launch. Normal Runtime has no writable endpoint.
The maintenance image omits the ordinary paired-store ABI marker, so it cannot
be admitted as an ordinary paired firmware update. Its artifact is separate from
both the generic seed and Runtime OTA candidates. Installing/running it on an
actual device is not performed by this task and requires a separately selected
owner deployment procedure; do not replace an active Runtime image blindly.

After the owner has deliberately entered that image, the host entry point is:

```sh
/tmp/provision-input --install --profile /owner/private/profile.json \
  --port /owner/explicit/serial-device --expected-source EXACT_40_HEX_SOURCE_SHA \
  --time-server time.example.invalid
```

Use actual owner-selected paths/server and the exact maintenance artifact source.
No port discovery, serial reset command, firmware flashing, partition operation
or device reboot is performed by the client. It validates the profile locally
through the existing packager before opening the explicitly named serial port.
The maintenance handshake must match the expected source before profile bytes
are sent. A one-use challenge, bounded lengths and SHA-256 cover framed transfer
integrity; they do not authenticate a hostile serial peer. This is an explicit
physically controlled maintenance interface, not a public network service.

A partial/corrupt/oversized/timed-out request never invokes NVS installation.
Input is wiped on completion/rejection/timeout; responses contain fixed status
labels, never credentials or payloads. After any uncertain send/response, the
client does not reconnect, retry, reset or erase. Inspect selected state before
another invocation. Transport tests connect the real client to the production
framing/installer through subprocess pipes and simulated NVS only. Native target
CI compiles the separate image, checks its source/maintenance markers and proves
that the normal paired image lacks the maintenance endpoint. Hardware is UNRUN.


### Offline maintenance-entry/restoration planner

`scripts/maintenance_plan.py` prepares reviewable entry/restoration payloads from
an explicit ABI1 inventory, a frozen private 16 MiB flash snapshot, and the
verified separate maintenance artifact. It opens files only; it has no device,
serial, reset, flash or erase implementation.

```sh
python scripts/maintenance_plan.py --inventory /owner/private/inventory.json \
  --snapshot /owner/private/frozen-flash.bin \
  --maintenance /owner/private/verified-maintenance-artifact \
  --output /owner/private/new-maintenance-plan
```

Inventory schema `riscrte.maintenance-inventory`, version 1, requires exactly:
`layout` (`riscrte-paired-16m-v1`), `flash_bytes` (16777216), integer `active_bank`
(0 or 1), canonical `runtime_version`, `running_firmware_sha256`, exact
`maintenance_source_sha`, and explicit `quiescent: true`. The ordinary Runtime
must support descriptor v2 (0.1.31 or later). The planner verifies the partition
table, pinned bootloader, confirmed OTA selection, active journal and active
firmware/store digests against this inventory. Pending/ambiguous transitions,
missing or incompatible inventory, unsupported near-wrap OTA sequences and
ABI2/app-data layouts are rejected. These checks cannot establish that a live
device still matches a stale snapshot; the owner must verify that separately.

The private output directory contains only an inactive-application entry image,
one alternate OTA page selecting it as NEW, and exact restoration copies of
those two regions, plus a plan and hashes. No health confirmation is fabricated.
NVS is neither read nor included in any output/restoration payload. The active
firmware/store, bank journal and partition table are untouched. Entry stages and
verifies the inactive image before changing the alternate OTA page; the original
confirmed OTA page remains intact. Restoration requires deliberate ROM mode,
verifies the expected maintenance image/sequence, restores the inactive image
before its old OTA page, and retains newly installed NVS input. Unexpected
ordinary provisioning invalidates the old restoration plan.

The plan includes offsets and preconditions, not executable flashing commands.
It does not install a maintenance image over running code. Keep the snapshot and
restoration files private; no owner snapshot is published or uploaded to CI.
Software tests use dummy snapshots and separately use freshly built artifacts,
checking deterministic plans, NVS exclusion and incompatible/missing inventory
refusal. All physical entry, installation, restoration and hardware checks remain
UNRUN and are not software-readiness prerequisites.

OTA selection/state handling follows the pinned IDF4.4.7
[selection implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bootloader_support/src/bootloader_common_loader.c)
and [boot transitions](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bootloader_support/src/bootloader_utility.c):
the largest eligible sequence selects `(sequence-1) % 2`; NEW becomes
PENDING_VERIFY and an unconfirmed later boot can mark it ABORTED. The planner
keeps the existing confirmed page and never marks maintenance VALID. It supports
only the exact raw image/layout formats validated by the existing candidate
helpers; it does not decrypt, sign or convert another deployment format.
