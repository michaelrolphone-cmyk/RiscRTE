# Optional generic file capabilities

This repository note describes the generic storage implementation. Delivery is
staged: generic interfaces and providers, optional Runtime integration, shared
default ELFs, then a reliable baseline. Existing storage paths in other
applications remain supported as accepted technical debt; further consumer
migrations are deferred until that baseline. Authoritative requirements and
delivery tracking are maintained in Workboard.

`storage.files@1` is an optional backend-neutral file service. Its public header
is `sdk/app/RiscStorageFilesV1.h`; `storage.files.catalog@1` discovers only the
logical namespaces already authorized to the caller. Neither contract contains
media identities, transport choices, mount/format/power operations, driver names,
or storage-path selection. Applications use opaque namespace IDs and ordinary
paths within the selected namespace.

The Runtime broker is default-off (`RISC_RUNTIME_STORAGE_FILES=0`). A product
that needs it explicitly opts in and provisions the desired providers. Generic
Runtime and projects without storage do not acquire a storage dependency. The
preference facade is independently optional (`RISC_RUNTIME_PREFERENCE_STORE`).

A file grant has explicit policy fields:

```json
{"capability":"storage.files","api":1,"instance_id":1,
 "backend_capability":"storage.volume","backend_instance":12,
 "label":"Documents"}
```

`instance_id` is a positive generic namespace. `backend_capability` is exactly
`storage.files` for an ordinary generic provider or `storage.volume` for a legacy
provider adapter. `backend_instance` selects its exact provisioned instance;
an optional `backend_provider` selects an exact provider ID when multiple software
providers have instance 0. Instance 0 is a valid backend identity and is not a
wildcard. These implementation fields never reach the application table or catalog.
`label` is nonempty presentation text, at most 48 bytes. Existing providers are
admitted through the normal validated graph, dependency and demand-lifecycle
rules. Missing/ambiguous mappings fail closed; no provider is guessed by order.
A preserving update cannot silently retarget an existing logical file namespace.

A catalog grant is `{ "capability":"storage.files.catalog", "api":1,
"instance_id":0 }`. Its snapshot/get calls enumerate authorized namespaces
without activating a provider or probing availability. Revisions are grant-local;
a foreign snapshot is stale. Unavailable namespaces remain discoverable. A files
acquisition with instance 0 succeeds only when precisely one namespace is
explicitly authorized; otherwise the application selects a catalog namespace ID.

All operations pass through generation-safe invocation/owner contexts and a
per-grant handle wrapper. Releasing a grant with open handles is refused. Failed
close or RETAINED status retains the invocation, provider and dependencies until
restart, without cleanup retry. Clean missing, unavailable, busy and I/O statuses
are distinct. Direct generic providers and the legacy adapter implement the same
public contract. The legacy adapter requires its authoritative metadata/custody
suffixes and never treats a boolean failure as evidence that a path is missing.

The ABI covers open/read/write/seek/info/sync/truncate/close, path stat/mkdir/
rename/remove, directory iteration and cursor operations. There is no raw export
lease, mount, device type, refresh, media or migration API in the generic surface.
Old volume clients and providers retain their existing ABI unchanged.

Host verification (not hardware qualification):

- `test/run_storage_files_runtime_test.sh`: real Runtime namespace admission,
  ambiguous default rejection, metadata-only catalog, mapping independent from
  raw instance ID, owner/context/token isolation and retained-custody exit.
- `test/run_storage_files_adapter_test.sh`: production adapter CRUD, bounds,
  directories/cursors, precise error distinctions and retention faults.
- `test/run_optional_storage_disabled_test.sh`: storage-free Runtime admission
  succeeds; all new unselected brokers reject, and broker symbols are absent.

## Optional internal source implementations

`RuntimeFiles` is an explicitly bound internal client of the same file ABI.
The binding, provider and namespace root remain alive through open handles and
retained failures. Unbound use is unavailable. Metadata consumers use this boundary. Loader, configuration and scratch
consumer wiring remain separate increments.

`PosixFiles` encapsulates ordinary POSIX operations. Linux no-replace rename uses
`renameat2(RENAME_NOREPLACE)`; platforms without that primitive report unavailable.
A prechecked destination followed by overwriting rename is never used. Namespace
assembly must own its root and serialization. The root string is borrowed and
immutable for the provider lifetime. Special files and observed symlink aliases
are rejected; this provider is not an OS sandbox against hostile concurrent
ancestor-directory replacement.

`NativeSpiffsFiles` explicitly selects the pinned IDF flat VFS behavior inside the
provider implementation. CREATE uses exclusive creation, reopening an existing
file without creation only after EEXIST. TRUNCATE uses checked `ftruncate`;
APPEND seeks to the current end before each serialized write. There is no
remove/recreate recovery. `info` reports actual native position, including a
position clamped by native truncate, rather than an assumed cached position.
Modification time is valid only when the native SDK stores it; disabled timestamp
support never advertises a synthetic epoch value. The namespace root cannot be
opened or created as a regular file.
Root directory enumeration returns flat relative object names, including slash
characters already present in those names. It does not synthesize a hierarchy:
non-root directory operations and mkdir are unavailable. No-replace rename is
also unavailable on this backend. Configuration replacement must use a separate
provider operation preserving its existing backend guarantee.

No full-image allocation or copy is introduced. Removing cached file positions
reduces per-handle state; querying position adds a native seek-current operation
to `info`, with no added operation per ordinary read. Native directory position
changes are checked against the reported cursor, rather than assumed successful.

`test/run_native_spiffs_files_test.sh PATH_TO_PINNED_IDF` runs production provider
code over actual SPIFFS RAM flash and unchanged extracted IDF VFS callbacks.
It pins IDF `fff9895c82d744c7237be8847347bdd1b07c6643` and SPIFFS
`ad902cadceb39d0825a97e25ecb5867641f606ba`. Formatting is confined to fixture RAM.
Full ASan/UBSan covers the provider and bridge. Only alignment checking is
suppressed on unmodified SPIFFS C sources, whose pinned IDF configuration uses
byte-packed object-index tables. This is native implementation semantics testing
on a host, together with a separate Xtensa compile probe; it is not hardware
qualification or a claim that subsequent Runtime consumer integration is done.

### Internal metadata identity and source selection

The optional `storage.files.identity@1` supplement returns canonical opaque
identity values for paths and open handles in the same file-provider binding.
The current representation is a 72-byte value; that size is an ABI design choice,
not a requirement that a backend use any particular native identity mechanism.
POSIX/native adapters preserve their prior device/inode/size/time comparison
(including Linux nanoseconds) without exposing those meanings to consumers.
This is change/replacement evidence, not a content hash, frozen snapshot or new
durability guarantee. Missing identity support remains explicitly unavailable.

A trusted `RuntimeFileSources` map binds exact logical roots to already-bound
sessions. Missing or duplicate roots cannot select a source. Active and staged
roots are resolved separately; copying a Runtime port cannot silently substitute
an active source for a staged one. Consumers must check the whole map's retained
barrier before every lookup/I/O, retaining all dependent sessions until restart.
The map, its entries, roots and provider bindings are immutable borrowed platform
configuration for their complete lifetime.

The internal read-only `RuntimeFileView` selects one exact source root and fences
all callbacks against retention anywhere in the shared source map. A known file
or directory token whose close cannot dispatch is retained as well; a metadata
consumer never forgets that token or rebinds the provider. The view is three
borrowed pointers (12 bytes on the native 32-bit target), with no image buffer.
The explicit-provider JSON entry shares the original 65,537-byte allocation,
512-byte read limit, five-second deadline and 32-KiB/two-millisecond/final-remainder
yield checkpoints. The path-only JSON/cohort entries are removed. Runtime boot/board/driver/app
metadata, inventory and cohort comparisons now require this explicit view.
Missing source binding fails admission before I/O.

### Variable-capacity directory enumeration

The additive `storage.files.directory@1` supplement uses the same provider context
and opaque directory token. `next` accepts a caller-owned name buffer and reports
required bytes including NUL. `RISC_FILES_DIRECTORY_CAPACITY` leaves the entry
unconsumed, including a null/zero-capacity size probe; seek, rewind and close
invalidate that position normally. The fixed `storage.files@1` entry remains
unchanged. A legacy provider without the supplement can enumerate fixed names
when the caller supplies at least the existing fixed capacity; a present failing
supplement never falls back. Provider physical path/name limits still apply.

Cohort inventory preserves its existing 256-object and depth-ten bounds with one
open cursor at a time. Its additional queue is 256 six-byte metadata records
(1,536 bytes), allocated using the existing metadata allocation policy. It accepts
both real hierarchies and flat slash-containing names; it does not fabricate
hierarchies for a flat backend. The existing 193-byte admitted manifest path
buffer is preserved, including names longer than the fixed file-entry field.

### Metadata consumers and native assembly

Installed-artifact reads retain admitted-path filtering and compare opaque path,
opened-file and post-read identity. Legal short reads fill the existing bounded
512-byte local buffer. Bytes are returned only after complete read, identity
verification and successful close. There is no extra executable-image buffer.
Every Runtime/candidate sharing the source map observes any retained source;
failed cleanup cannot reset or rebind that map.

Native assembly selects immutable active/staged roots before metadata reads. Its
state is 960 bytes in the pinned Xtensa compilation, prefers PSRAM and has no
internal-memory fallback when metadata PSRAM or
paired-bank policy requires it. The state remains allocated through restart; it
never owns a full image copy. The immutable embedded VFS now implements pathname
stat as well as descriptor stat, and advertises no modification timestamps.
Bootstrap digests and bank receipt/cohort metadata use the injected source map.
Provider-owned partition/provisioning operations remain in their backend.

Qualification distinguishes host behavioral tests, actual pinned SDK translation
unit compilation, firmware linking and hardware execution. The metadata target
runner compiles Json.cpp, Runtime.cpp, NativeBootstrap.cpp, NativeBankStore.cpp and
main.cpp against configured SDK headers; this alone is not a firmware build.

The identity and variable-capacity directory supplements are currently wired only
through trusted internal provider bindings. The application broker does not yet
materialize or grant these supplement capabilities. Their reusable ABI headers
do not imply application acquisition support.
