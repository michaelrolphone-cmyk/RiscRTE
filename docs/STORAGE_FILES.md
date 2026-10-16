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
retained failures. Unbound use is unavailable. This boundary is independently
qualified; metadata, loader, configuration and scratch consumer wiring follows
in separate increments.

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
