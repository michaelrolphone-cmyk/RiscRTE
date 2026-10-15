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
