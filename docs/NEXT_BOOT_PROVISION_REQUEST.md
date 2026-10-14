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

The private NVS namespace `rte_provreq` owns three keys; it never erases any
namespace, partition or unrelated key:

- `request`: the original version-1 blob, with a 160-byte little-endian header,
  complete profile (at most 16384 bytes), and required explicit time input (at
  most 384 bytes). Maximum record size remains 16928 bytes.
- `receipt`: a version-2 terminal record, encoded as exactly 576 lowercase hex
  characters plus NUL using `nvs_set_str`. Its 288 decoded bytes contain no
  credentials. IDF stores the string on one page rather than using multipart
  blob replacement.
- `state`: one permanent `nvs_set_u8` scalar: READY=1 or ACTIVE=2. Only this
  small scalar is ever updated in place. The native adapter refuses an in-place
  set for either blob/string and cannot erase this marker.

The header binds store ABI, native firmware size/hash, original source-store
hash, profile hash and whole-record hash. SHA256 detects corruption; it does
not authenticate a feed or expand its authority. No timestamp or guessed time
server is introduced. Native ownership requires an idle, confirmed, non-pending
active pair before any request can be consumed or compacted. Image-mode
requests also require the confirmed store to equal their target-store digest.

ESP-IDF 4.4.7 writes a replacement blob before reclaiming its previous
generation. Two actual 125/132-file private requests do not fit together in
24 KiB NVS. Its failed multipart update cleanup can also erase the old
version's chunks under alternate generation and space pressure. A same-key
small-receipt replacement is therefore unsafe; it is not used here.

### Durable selection protocol

Every entry point (queue, public status and boot input) uses the same selector:

1. Read the terminal string first. A valid terminal selects CONSUMED only for
   its matching confirmed bank. Any unavailable, malformed or unsupported
   terminal stops selection without reading the payload or legacy input.
2. If the terminal is missing, inspect the scalar. ACTIVE or an uncertain/invalid
   scalar blocks all payload reads. READY permits a validated pending payload,
   but a missing/invalid payload stays BLOCKED and never restores legacy input.
3. Only a missing terminal, missing marker and missing request retain the old
   pre-initialization owner-bootstrap path. Existing version-1 requests with no
   marker remain readable.

Typed getters alone are not absence evidence in pinned IDF: they can return
NOT_FOUND for an existing key of another type. The adapter first enumerates
key metadata and requires the exact expected type. A populated namespace with
a failed iterator allocation is unavailable, never Missing; malformed keys
are not erased by this check.

A new initial request is copied, written, committed and exactly read back
before READY is established and verified. A failed first enqueue therefore
cannot strand an empty initialized queue. An exact replay of a legacy
version-1 request initializes READY before acknowledging it. Normal exact
replays perform no writes and return the original request identity.

A different request may replace only a proven-consumed record:

1. Freshly write the terminal string, commit and verify all decoded bytes.
2. Set ACTIVE, commit and verify its exact scalar value.
3. Erase only the consumed/staged `request`, commit and verify absence using
   metadata only, without reading potentially half-erased payload bytes.
4. Freshly write the complete next version-1 request, commit and exactly read
   it back while the terminal still selects the previous consumed request.
5. Set READY, commit and verify it, then erase the terminal, commit and verify
   absence. Only now acknowledge publication of the new pending request.

A request hidden by an authoritative terminal is staged and unadmitted, so a
retry may discard it. Once the terminal is removed, the new pending request
cannot be erased by this path. An identical consumed request is a separate
idempotent-replay case: it returns the original identity while keeping the
terminal and its replay suppression intact; it does not publish a new request.

Each uncertain set, erase, commit, readback or absence check stops immediately
with UNKNOWN. A returned NVS mutation error can leave IDF's live page cache
unsafe to read even though raw flash is recoverable. The native bridge therefore
keeps queue, status and boot-input access UNKNOWN/unavailable for the rest of
that process, without reading any queue records. It also quarantines the shared
RiscNvs startup/failure latch: unrelated KV, settings, bootstrap and owner-install
reads, probes and writes return unavailable/IO before opening NVS. This prevents
a false Missing result from the damaged live SDK cache becoming a saved default.
The queue holds the native shared-resource lease during mutation, excluding
in-flight radio setup. A separate atomic quarantine flag also fences new Wi-Fi,
BLE and radio-IQ PHY initialization: SDK PHY calibration can access NVS even
with Wi-Fi RAM configuration. Existing cleanup/drain paths remain available.
Ordinary startup failures retain their previous behavior and do not set this flag. Only the next ordinary boot
reinitializes NVS and reconciles durable selection; no in-place NVS reset,
formatting, forced reboot or same-boot status retry clears the fence. No later write is attempted after a failed step. Interrupted states
select the old consumed request, a matching terminal, or the complete new
pending request. A retained terminal supplies no bootstrap profile/time, so
legacy owner input cannot replay. Wrong-bank, rollback, unconfirmed, unrelated
receipt or native-firmware identity mismatches cannot authorize replacement.

IDF may erase a string whose data CRC fails during a read. ACTIVE preserves the
selection fence if that removes the terminal: repeated reads and reboot remain
BLOCKED without touching a staged payload. READY remains after publication, so
later loss of the pending record cannot restore owner-bootstrap input either.
Corrupt records fail closed; automatic reconstruction of lost receipt identity
is deliberately not attempted.

### Terminal record (version 2)

All integers are little-endian uint32. Magic remains `RTEPRQ1\0`; the explicit
version distinguishes the records. Version-1 pending records remain readable.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | Magic |
| 8 | 4 | Version 2 |
| 12 | 4 | Total size 288 |
| 16, 20 | 4 each | Original profile and time lengths |
| 24, 28 | 4 each | Original store ABI and native firmware length |
| 32 | 32 | Original source-store SHA256 |
| 64 | 32 | Native firmware SHA256 |
| 96 | 32 | Original profile SHA256 |
| 128 | 32 | Original full version-1 request SHA256 |
| 160 | 32 | Original target-store SHA256 (zero for legacy per-file profiles) |
| 192 | 32 | Confirmed consumed-store SHA256 |
| 224 | 32 | Original time-input SHA256 |
| 256 | 32 | SHA256 of all 288 bytes with this field zeroed |

The decoded receipt retains no JSON, SSID or password. This is logical record compaction,
not secure erasure of old flash contents. Exact replay requires the original
profile/time hashes and lengths and returns the original request identity.
Changed time input is a different request even if its profile is unchanged.
Malformed, unsupported-version or nonempty unavailable records are never
interpreted as missing or overwritten. The 16384-byte profile / 384-byte time /
16928-byte record bounds remain unchanged. NVS still needs its reserved GC page,
chunk headers and other live keys: compaction does not promise success in an
arbitrarily populated partition. If the control or new record cannot fit, the
operation stops UNKNOWN and preserves a known durable selection for recovery.

The relevant pinned upstream implementation is ESP-IDF 4.4.7 commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`:
[`Storage::writeItem` and `writeMultiPageBlob`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/nvs_flash/src/nvs_storage.cpp),
[`PageManager::requestNewPage`](https://github.com/espressif/esp-idf/blob/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3/components/nvs_flash/src/nvs_pagemanager.cpp).

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
SD path to the admitted internal image digest. The combined Runtime hashes the
actual copied SD app and driver snapshots against that digest before loading or
relocation; a filename alone is not accepted as byte verification. The existing
external-only source contract remains distinct from these internal-fallback
mirrors. A bootfs profile is not a live generic storage volume or a callback
usable while provider custody exists.

## Composition dependencies

The tested combined source already includes boot-owned/default app
`internal_fallback` parsing and source-role/cleanup admission, bounded schema-4
provisioning with up to 160 complete-store entries, preserving app-update audits,
and actual app/driver SD snapshot digest checks. These are integrated behavior,
not future dependencies. This queue checkpoint builds on the existing combined
Runtime and does not change its image layout or partition sizes. Product
qualification must still bind the complete compiled native image and store
cohort to the final published Runtime revision; no future publication SHA is
assumed here.

## Evidence and remaining qualification

This source checkpoint is published with exhaustive qualification explicitly
pending. Normal/sanitized core, bridge, native NVS and focused PHY tests pass;
ESP32-S3 target compilation and the assertion-enabled real-NVS sanitized
capacity/repeat/CRC suite pass. The frozen target/assertion cut matrices continue
unchanged and are not represented as complete. See
`docs/evidence/request-compaction/README.md` for exact completed and partial
evidence. Product or hardware qualification must not infer completion from the
source publication.

- `test/run_request_queue_test.sh`: corrupt every record byte, copied inputs,
  source binding, replay, pending refusal, consumed replacement, torn/uncertain
  writes, and no unrelated erase
- `test/run_request_queue_compaction_test.sh [feed.json ...]`: fixed terminal
  format, original identity and credential removal, every-byte corruption,
  wrong-bank/native/pending fences, every persistence-operation cut and failure,
  finite-entry reserve-page ceiling, maximum bounds/refusal, retry and optional
  actual 125/132-file feed inputs (only synthetic test Wi-Fi is injected).
  This conservative entry model is not the upstream NVS GC engine.
- `test/run_request_queue_nvs_test.sh`: clean per-run build of pinned IDF 4.4.7
  NVS plus its upstream flash emulator and the actual production queue/storage
  adapter. Covers actual inventories, repeated changes, maximum sizes, both old
  generations, unrelated-key pressure, every injected 4-byte write / sector-erase
  boundary, immediate UNKNOWN/quarantine with no NVS calls, untouched-flash
  reboot, scalar transitions and the independently reproduced string-CRC
  self-erasure case. Unrelated string, 768-byte blob and u32 pressure keys are
  checked after recovery; their native getters cannot turn live-cache errors
  into saved defaults.
- `test/run_native_provision_request_test.sh`: real adapter with NVS boundary
  model, namespace isolation, owner checks, stale/corrupt/pending routing,
  successful receipt suppression, public target-image digest, commit-UNKNOWN
  ordinary-boot reconciliation, shared-resource refusal, and allocation/NVS-full failure
- `test/native_nvs_shim/run_test.sh`: global quarantine rejects KV/settings/
  bootstrap reads, probes and writes without NVS calls; repeated wrapped startup
  cannot reset quarantine or trigger Arduino partition erasure.
- `test/run_nvs_quarantine_radio_test.sh`: Wi-Fi, BLE and IQ initialization gates
  block indirect PHY/NVS access while existing active cleanup remains available.
  Includes the existing IQ ROM/layout variants; its focused native checks are
  independent of the baseline IQ port assertion recorded in the evidence.
- `test/run_preserving_policy_test.sh`: metadata mutation/fault matrix
- `test/run_native_bank_test.sh`: existing private transaction regressions
- `test/compile_native_provision_request_target.py`: single-process ESP32-S3
  adapter/policy compilation; no flash or device access

The corruption model includes malformed/checksum-invalid logical records,
a single terminal-string data fault (including IDF's destructive CRC read), and
loss of a published payload while the marker remains intact. It does not promise
recovery from arbitrary simultaneous destruction of multiple keys, the namespace
or a physical flash sector. The upstream emulator models 4-byte write boundaries
and complete sector-erase failures, not partial physical sector erasure.

Host models and target compilation do not qualify power loss on real
NVS/SPIFFS/LittleFS, combined firmware RAM/stack budget, Wi-Fi/TLS on hardware,
SD insertion/removal, or physical rollback. Product exact-head combined builds
and complete-store admission remain required.
