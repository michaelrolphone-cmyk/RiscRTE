# Generic catalog payload/container contract 1.0.0

This is a **schema and offline validation contract**, not an installer, firmware
release, new serial command, or hardware qualification. It extends the catalog
side of the frozen [RTEF1 contract 1.0.0](../../catalog-serial/1.0.0/README.md),
published at Runtime commit
[`891c4a9b20cc03fb1e6899ad287e9e22da9f64de`](https://github.com/michaelrolphone-cmyk/RiscRTE/blob/891c4a9b20cc03fb1e6899ad287e9e22da9f64de/docs/catalog-serial/1.0.0/README.md).
It does not change firmware ABI or admission.

A full payload is the deterministic inventory of **every storage file required
by one exact release**. It does not mean a filesystem image, folder replacement,
recursive copy, formatting, deletion, or rename. Files absent from the inventory
are unrelated and remain untouched by the per-file transfer. A publisher must
build the inventory from the release's complete requirements; schema validation
cannot discover a file the publisher forgot to declare. Never derive the list
from whichever files happen to be on a connected device.

## Deliverables and quick validation

- `manifest.schema.json`: strict JSON Schema 2020-12; all objects reject unknown fields.
- `descriptor.schema.json`: small, proposed catalog API reference to exact manifest/container assets.
- `context.schema.json`: input to this reference validator, representing trusted catalog/profile facts.
- `validate.py`: bounded shape, semantic and byte validation.
- `make_examples.py`: deterministic generation of five complete synthetic examples.
- `test_validate.py`: executable positive and adversarial coverage.
- `examples/{bootstrap,update-after,update-before,update-before-readonly,flat-root}/`: canonical manifest, context,
  descriptor and complete container bytes for each fixture.

Use Python 3.10 or later and `jsonschema==4.26.0` (`requirements.txt`). From this directory:

```sh
python3 test_validate.py
python3 validate.py examples/bootstrap/container.zip \
  --context examples/bootstrap/context.json --allow-fixture
python3 make_examples.py --output /tmp/catalog-payload-examples
```

All examples are visibly `fixture: true`. Their firmware is harmless text, their
three payload files total 273 bytes (binary, empty and JSON), and namespace `7`
is **synthetic**. They are not installable X4, Waveshare or T-Watch releases.
The validator rejects them unless `--allow-fixture` is supplied. That switch is
for offline tests only.

## Container and content identity

The v1 container is a ZIP using **ZIP_STORED**, with exactly `manifest.json` and
one regular member for each listed firmware/payload source. No directories,
links, special files, compression, encryption, extras, comments, or unlisted
members are allowed. Members are sorted by UTF-8 filename bytes, timestamped
1980-01-01 00:00:00, Unix regular mode 0644. Source names are portable ASCII
relative paths. No extraction is needed: the reader streams selected members.

`manifest.json` is UTF-8 without a BOM: object keys sorted, two-space JSON
indentation, Unicode unescaped, no NaN/Infinity, and one final LF, as produced by
`canonical()` in `validate.py`. Duplicate JSON members are rejected. Arrays
retain their defined semantic order. Regenerating the examples is byte-for-byte
deterministic. The external descriptor pins the exact manifest and container
length/SHA-256, avoiding a self-hash cycle. The validator additionally enforces
trusted manifest/container hashes supplied by the catalog context.

The catalog release identity includes catalog ID and revision, repository,
numeric upstream release ID (a decimal string), source commit and human-readable
tag. A tag or URL alone is not immutable. Resolve a frozen catalog revision and
verify the pinned digests; an edited release asset must fail, even at the same
URL. The firmware has its own identity, version, source commit and complete
artifact list. The payload has its own identity and owner; `inventory_sha256`
hashes canonical `payload` with only `inventory_sha256` omitted. Every firmware
artifact and payload file has a source member, exact byte length and SHA-256.

Payload totals count only destination files and their bytes, including empty
files, not firmware, metadata or storage overhead. The reference reader caps
metadata at 4 MiB, payload files at 4096, archive members at 4353, and container
data at 2 GiB; a trusted client context may impose smaller limits. These are
reference-validator implementation limits, not universal storage-service limits. Sizes are JSON safe integers;
namespace IDs are exact decimal strings in `1..18446744073709551615`.

## Generic volumes, bindings and compatibility

`product` gives exact accepted product, hardware profiles/revisions, storage ABI,
wire protocol and serial contract. A logical volume names its required generic
transfer operations, required hierarchy and naming semantics. It also identifies the target
firmware's consumer and pinned evidence that this consumer can use the files.
Uploading bytes alone does not install, activate or execute a payload.

Each `(firmware_id, logical volume)` has exactly one explicit binding to an
admitted opaque namespace ID, backed by a pinned evidence record. Two volumes
may not alias the same namespace within a firmware. Bindings must be known to
the trusted product profile; a manifest cannot grant itself storage authority.
The target firmware always needs a mapping; a transfer-before-flash plan also
needs the installed firmware's mapping. Missing mappings fail closed.

Do not derive routing from `SD`, `SPIFFS`, `LittleFS`, a display label, a provider
instance number, a mount path, or the numerical value of a namespace. No backend
label or physical root is a routing field in this format. `context.namespaces`
contains the exact admitted capabilities and stricter provider bounds from the
selected firmware profile. It is not an alternate manifest-defined admission.
An enumerated namespace can still be unavailable; live provider checks remain
necessary. No fallback namespace is permitted.

The frozen Wave/T-Watch contract distinguishes writable IDs 2 and 12 from
physical implementation details. It does not establish an application consumer
for arbitrary files uploaded into private serial storage. An actual package
must establish its own usable destination mapping. This schema deliberately
contains no final X4 `.123` mapping and does not depend on that product's CI.

## Paths, inventory, parents and ownership

Destination paths are absolute **namespace-relative** paths, not physical VFS
paths. Preserve exact strict UTF-8 bytes; do not normalize Unicode, change case,
trim whitespace, URL-decode, or prepend a physical mount prefix. Reject root `/`,
missing/empty components, `.`/`..`, trailing separators, backslashes and ASCII
controls/DEL, as required by RTEF1. Respect the 511-byte wire bound and the selected
provider's smaller UTF-8 path/component bounds. Extra character or trailing-dot/space
restrictions apply only when explicitly supplied by that trusted provider profile.
The private serial provider in the frozen contract has a 490-byte path limit.

Files are sorted by `(volume, UTF-8 destination)`. Volumes sort by ID. Bindings
sort by `(firmware_id, volume)`. Firmware artifacts sort by source. Parent
directories are the **complete exact set** of file parents, sorted by
`(volume, depth, UTF-8 path)`, so ancestors precede descendants. Root is implicit.
No empty extra directory is created. A root-only inventory has no parent
directories and requires no hierarchy or MKDIR; a flat provider is valid for it. File/directory conflicts, duplicate sources,
duplicate destinations and provider-specific aliases are rejected. The v1
case-insensitive mode supports ASCII paths only and detects ASCII case aliases;
other naming semantics require explicit support in a future version.

Each file explicitly chooses `create` or `replace`; there is no upsert/fallback.
Every file names the release owner responsible for the resulting payload.
`create` requires a missing destination and no `previous` guards. `replace`
requires an explicitly selected existing regular file; it may replace older or
customized content without a mandatory predecessor receipt. Optional `previous`
guards can constrain current owner, size and/or SHA-256. Check each supplied guard
against trusted ownership records or actual bytes before PUT. Omitted guards are
not invented requirements, and the new owner field is not proof of past ownership.
The manifest does not itself authorize replacing arbitrary user files. Honor the
explicit selection/authorization for that file. Existing content blocks create,
and absent content blocks replace. Do not automatically switch modes after failure.

`ensure-directory` is a declarative host action, not a new wire command. For each
listed parent in order, use RTEF1 STAT; accept an existing directory, reject an
existing file, and use the existing single-directory MKDIR only for an
authoritative NOT_FOUND. Unavailability, busy, retained custody and I/O failure
are not absence. Do not recursively enumerate or replace directory trees.

## Catalog-selected ordering and firmware effects

The catalog selects a supported flash method and validates its exact artifacts,
volume effects, erasure disclosure and target mapping. `method_id` refers to an
already supported catalog flash implementation; this document supplies no flash
command, guessed offset or new device operation. `volume_effects` covers every
logical volume, separately from `settings_effect`. The pinned install record
provides evidence for each effect and the selected method. The `factory` label
does not infer an erase effect; the exact supported method and evidence determine
what is preserved, erased or unknown.

Two orders are expressible:

1. `firmware, payload, verify`: suitable for initial bootstrap, or for a combined
   update where transfer follows flashing. Factory installation is allowed with
   an accurate existing-data/settings erasure disclosure. Re-discover and verify
   the target firmware's actual admitted namespace/provider before file mutation.
2. `payload, firmware, verify`: allowed only with a verified installed-firmware
   mapping and explicit `preserved` evidence for every required destination
   volume across that exact flash method/firmware transition. `erased` or `unknown`
   rejects this plan before mutation. Read back again after the firmware step.
   Only the transfer-stage mapping needs write/MKDIR operations and write budget.
   The final mapping may be read-only if it supports STAT/GET and the target
   consumer can use the retained payload; no post-update write is required.

These choices belong to the catalog. A combined update is complete only when
all required payload files are verified and usable by the target consumer
**after** the update. A factory image may erase data; no blanket guarantee that
every setting survives every firmware installation is made. A preserving
payload volume does not imply preserving settings/internal data. If no supported
ordering/method can provide the final usable payload, do not offer that combined
update as ready. This schema does not move the user's initial X4 bootstrap gate
or later X4 → Wave → T-Watch hardware validation gates.

## Static validation and live execution boundary

`validate.py` returns `static-preflight-passed`, never `installed` or `ready to
flash`. It checks schema, immutable trusted identities, firmware/product/provider
compatibility, method support, unique mapping, complete ordered inventory,
source lengths/hashes, operations, totals, parent paths, ownership declarations,
capacity budget and ordering. Run this **before any mutation**. Unknown schemas,
fields or operations fail closed. The context is a reference validator input,
not a required new service, review process, signing infrastructure or API endpoint.
The builder may obtain the same facts from its existing trusted catalog/profile
state. It must not manufacture trust pins by hashing an untrusted manifest. Matching
these input hashes checks consistency, not authenticity of the context itself;
the application supplies that trust boundary from its existing catalog/profile
source. The fixture generator self-seals only synthetic test data.

The returned live gates are existing operational requirements, not evidence
that this offline tool contacted hardware. The installer must:

- Hold one owner-trusted client/session; prevent competing writers. Reconfirm the
  selected device/installed firmware and the supported flash method/permissions.
- Verify the actual admitted IDs/writable flags and provider availability. Compare
  trusted path/file limits and available write budget (after private-state reserve
  and metadata overhead). RTEF1 has no free-space query; do not invent one or infer
  capacity from a label. A profile budget is a bound, not a live fit guarantee.
- STAT every destination and parent before the transfer stage; reject all known
  conflicts first. For replace, confirm explicit selected-file replacement and
  check any declared previous-content/ownership guards before destructive PUT.
  Recheck after flash, and immediately before each mutation.
  Do not claim all runtime errors can be predicted or all writes are atomic.
- After any firmware change, establish that the target firmware is running, reopen
  a supported session and recheck its exact mapping/provider before file mutation.
  State observed before flashing is not proof of state afterward.
- Transfer only the validated immutable bytes and declared file operation. Require
  acknowledged COMMIT/count/CRC32, then full GET/READ/CLOSE and host SHA-256 readback.
  After the last firmware step, verify **all** required files and target usability.
  Keep source, software tests, publication and physical hardware results separate.

A complete combined update may have been partially applied when a later step
fails. Return incomplete/error with exact per-file receipts, not a whole-release
success. Record release/manifest/container hashes, firmware identity, namespace,
path, operation, expected/actual bytes/hash, stage and acknowledged outcome.

## Failure, interruption and recovery

RTEF1 replacement truncates the selected existing file at PUT. It is in-place,
destructive and non-atomic. Failure can leave an empty, partial or unverified
selected file. ABORT does not restore it, delete a partial create, undo parent
directories or roll back earlier files. Preserve unrelated files throughout.

On definitive errors the pinned client may use its existing best-effort ABORT
rules for a known handle. On uncertain responses, timeout/disconnect, malformed
frames, wrong session/sequence/offset, −6 or −10, stop and mark the session unusable.
Do not automatically retry, reconnect, reset, delete or overwrite. A lost PUT
acknowledgment can mean truncation occurred; a lost COMMIT can mean completion.
Inspect custody and the selected destination before choosing an explicit recovery
transfer. Readback may prove completion; a partial create needs a separately
reconciled selected-file replacement plan, not a blind replay of exclusive create.
No transactional rollback or power-loss durability is promised.

## Builder/API handoff

The current catalog tool's optional `release.payloads` descriptor uses
`target: sd | spiffs | littlefs`, `format`, `destination`, `phase` and `required`.
It is **not this format** and does not implement this generic installer contract.
Do not cast those labels to namespace IDs or claim it already supports this flow.

The focused adaptation is to add a separate release-level `payload_manifest`
reference with the shape in `descriptor.schema.json`, resolving exact existing
release asset names for the manifest and container. Preserve unrelated catalog
fields and revision checking. Fetch/verify both assets, match `manifest.json`
inside the container to the standalone manifest digest, and validate this version
before presenting its plan. The trusted release snapshot must associate this
reference with the exact firmware artifacts, product profile, admission mapping
and supported flash method. Keep legacy records readable without silently
converting ambiguous backend descriptors.

The website can implement the same JSON Schema and semantic checks in its own
language; it does not need to run this Python tool. Expose complete file count,
bytes, per-file destinations and create/replace behavior, the chosen flash/transfer
order, and the actual erase warning. Implement only the existing supported flash
method and frozen RTEF1 operations. Keep a release incomplete until final readback
and target usability succeed; preserve existing authorization and hardware gates.

No real release descriptor or hardware admission is published by these fixtures.
