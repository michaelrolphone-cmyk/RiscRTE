# Preserving next-boot provisioning requests

`RISC_NATIVE_PROVISION_REQUEST=1` selects the provider-only
`platform.provision-request@1` ABI in `RiscProvisionRequestV1.h`. Default-off
images expose no request endpoint. Only explicitly selected boot providers may
require it; dynamic provider envelopes and app grants cannot include it. The
existing installed-catalog and bank-store ABI prefixes are unchanged.

The requester supplies a bounded, complete schema 1/2/3/4 provisioning profile
(the parser decides supported versions), explicit Wi-Fi credentials inside that
profile, an explicit `riscrte.sntp` configuration, and the exact active-store
SHA256. A trusted foreground owner is responsible for obtaining credentials and
source configuration. No UTC timestamp or guessed time-server default is saved.
The caller must drain storage work before submission. Queue success means only
that the next-boot request was persisted and read back. It does not select a
bank, report installation complete, or reboot. Queued work begins on the next
ordinary boot. After successful bank activation, the existing boot-owner safe
restart path remains responsible for entering the selected pair.

## Native request storage

One self-contained blob is stored under private NVS `rte_provreq/request`,
separate from owner-only `rte_bootstrap`, app namespaces, and app-data. Its
explicit little-endian header binds schema/lengths, current store ABI and
firmware size/hash, current store hash, profile hash, and whole-record hash.
SHA256 detects corruption; it does not authenticate or expand feed authority.
The exact complete private profile remains bounded at 16384 bytes. The optional
extra time input is bounded at 384 bytes and is required by this queue; header
size is 160 bytes. No partition changes, formatting or erase recovery occur.

An identical request replays without a write. A different pending request is
refused. Replacing the last request is permitted only when the confirmed active
pair carries that profile's successful provisioning receipt. A single retained
consumed request prevents an older unchanged owner profile from being reapplied.
A commit/write/readback failure returns UNKNOWN and requires a status read
before another submission. NVS capacity failure preserves existing data; there
is no promise that a near-16KiB request fits a populated 24KiB partition, nor that
an existing large blob can be replaced without NVS's transient free-space needs.
Malformed nonempty requests are never overwritten or interpreted as missing.

## Boot selection and admission

Before normal Runtime creation, the native adapter reads the request. Only a
verified request whose source pair still matches, or whose profile receipt is
already consumed, supplies a synthetic bounded BootstrapInput. Missing input
retains the existing owner-profile path. Invalid, stale, unavailable or pending
pair input suppresses owner-profile fallback and continues normal installed
boot, subject to the existing safety checks. It never uses unsafe cleanup as a
fallback opportunity. The existing fresh-time/SNTP, TLS and bounded download
coordinator remains responsible for transport.

A request activates `preserving=true` in the private whole-store transaction.
Before ordinary full graph and ELF admission, the candidate must preserve the
installed board, all boot choices, grants, namespaces, features, storage
ownership, and app/provider declarations. Manifest versions may stay equal or
advance monotonically; image bytes may change even when the manifest version
is unchanged. An existing explicit internal-fallback SD source may advance to a content-addressed path for that same identity/volume;
its digest must name the exact admitted internal fallback image. The first
opt-in composition is a separate authorized cohort, not a preserving request.
Cohort product/source repository/runtime/layout/native firmware identity remain
fixed; product version and its source revision may advance.

The request never uses owner-authoritative profile semantics. The ordinary
boot-owner path remains distinct. New request and internal-package platform
shapes have inert metadata-only bindings for candidate admission; no storage
worker or provider is started during that pass. The complete original inactive
pair transaction still owns firmware cloning, exact image/inventory/readback,
static ELF/import admission, readiness, selection, health and rollback. Native
firmware bytes, the active pair, NVS unrelated keys and app-data are not update
targets.

## Media boundary

The live catalog's internal fallback is `/appdata/.packages`, not bootfs. Its
selected-medium durable journal continues to choose SD first only before an
operation starts; absent/full clean SD may choose that internal volume. Failed
close/retained ownership never permits switching media. Updates keep their
original medium. Core whole-store download remains internal paired bootfs.
SD is an optional mirror; its complete transitive mount/ownership closure and
default app remain internal. The preserving guard binds the content-addressed
SD path to the admitted internal image digest. This checkpoint does not hash
SD bytes at load time; a content-addressed filename alone is not byte-level
verification of removable media. A bootfs profile is not a live generic
storage volume or a callback usable while provider custody exists.

## Composition dependencies

This checkpoint retains base `78a99f278015e35bc96a025cbb6672285b306197` for
independent review. Boot-owned/default app `internal_fallback` parsing and
source-role/cleanup admission are supplied by the separate Runtime checkpoint
`a297018e21e39f0206aedcd95efcbe8390545d17`; this base supports provider fallback
only. Compact schema-4/160-file product profiles are a separate integration
checkpoint, `579187d4fb7a512705d8238a9548851de2534109`. Neither is silently
included here. Combined product testing must cover app and provider mirrors,
actual SD-byte digest verification, and the enlarged complete-store inventory.

## Evidence and remaining qualification

- `test/run_request_queue_test.sh`: corrupt every record byte, copied inputs,
  source binding, replay, pending refusal, consumed replacement, torn/uncertain
  writes, and no erase
- `test/run_native_provision_request_test.sh`: real adapter with NVS boundary
  model, namespace isolation, owner checks, stale/corrupt/pending routing,
  successful receipt suppression, public target-image digest, commit-UNKNOWN
  reconciliation and allocation/NVS-full failure
- `test/run_preserving_policy_test.sh`: metadata mutation/fault matrix
- `test/run_native_bank_test.sh`: existing private transaction regressions
- `test/compile_native_provision_request_target.py`: single-process ESP32-S3
  adapter/policy compilation; no flash or device access

Host boundary models and target compilation do not qualify power loss on real
NVS/SPIFFS/LittleFS, combined firmware RAM/stack budget, Wi-Fi/TLS on hardware,
SD insertion/removal, or physical rollback. Product exact-head combined builds
and complete-store admission remain required.
