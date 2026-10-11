# Explicit provider/root binding

`RiscStorage::ScopedUserVolumeBinding` is a trusted broker-side prerequisite for
the shared File Browser and a future file-transfer service. It binds the existing
`ScopedUserVolume` to a real, already-admitted `RuntimeProviders::GraphV2` provider.
It does not create an app capability, boot policy, filesystem, mount or server.
No Watch or other product selects it in this checkpoint.

## Existing ownership and source

This unit is based on the local same-volume-copy checkpoint `fe5c2dac`, following
the root-scoped adapter and optional directory operations. It reuses the exact
existing `storage.volume@1` ABI and ProviderGraph acquisition/release mechanisms.
No Graph, Runtime bootstrap, native port, partition or application source changes
are needed for this isolated binding.

The current native code establishes important deployment boundaries:

- `src/main.cpp` mounts the selected module store at `/bootfs`, with four native
  descriptors and `format_if_mount_failed=false`; its comment explicitly states
  that this is not a production volume capability.
- `NativeBankStore.cpp` owns the paired stores and `/updatefs` staging mount.
  Those stores contain immutable admitted package/cohort data. Provisioning
  validates exact inventories and may replace the inactive bank contents.
- SPIFFS uses flat slash-containing object names rather than ordinary
  directories (`StoreFiles.cpp`). The selected bank policy also fixes its object
  name length at 32 bytes. A directory-capable mutable user volume cannot be
  inferred from this mount or from its apparent VFS path names.
- `NativeAppData.cpp` separately mounts the explicitly provisioned `appdata`
  LittleFS partition, without format or growth. `AppDataFiles` and Runtime bind
  those files to private namespaces. NVS settings have their own ownership.

This binding does not reinterpret any of those private or installed namespaces
as user files, mount them a second time, or make a writable alias of `/bootfs`.

## Configuration and lifecycle

The trusted owner supplies four copied values: admitted provider ID, provider
instance, existing exposed root, and display label. Root validation is exactly
the adapter's existing root policy: a non-root normalized absolute subtree,
bounded names/paths and no traversal or reserved staging names. Configuration
does no provider I/O. It must come from validated owner policy, never untrusted
application arguments or a remote request.

`begin(&table)` selects only that ID's `storage.volume@1` using
`GraphV2::acquireFrom`. A positive instance matches that exact hardware instance;
zero requires a unique provider with the selected ID. It does not search all
providers for a suitable first match. Missing or ambiguous admission fails.

After acquisition, the binding checks native custody and graph state, obtains
the granted interface, and constructs the existing adapter in fixed optional
storage. It verifies the already-existing root and required canonical extension
operations before publishing the copied table. It never creates a missing root.
Output remains untouched on failure. An ordinary missing provider, root, medium
or operation can be retried after successful cleanup and an explicit correction.

Two trusted non-mutating hooks identify the single serialized owner and whether
native storage/mount custody is safe. Adapter callbacks also check the graph's
activation state and the same generation-checked grant/interface. Borrowed table
access follows these checks. The native hook must include the provider's retained
I/O state and mount lifetime; a healthy graph alone is not proof of native safety.

`end()` first ends the adapter: close the reader, abort any staged writer, and
checked-close its directory. Only then does it release the graph grant. A
retained storage outcome keeps the adapter, grant, provider code, graph and
dependencies alive and permits no cleanup retry or replacement acquisition.
Failed activation that leaves graph/native custody unsafe is also retained;
the binding does not invoke global shutdown or targeted recovery automatically.

A distinct, graph-owned release-pending result occurs if checked adapter cleanup
finished but provider quiescence refused. The adapter's old contexts are already
revoked. Only `end()` may retry the same graph grant under the existing Graph
release contract; no filesystem cleanup is repeated. No new acquisition is
allowed until release completes. Native custody loss before/during that release
instead makes the binding terminal. Destruction is not a recovery procedure.

## Bounds and isolation

- One graph grant per live binding; no new graph or Runtime grant capacity.
- One existing adapter slot per active binding, within its four-slot process
  limit. Failure to obtain a slot cleans up only the new binding's graph grant.
- At most one reader, one exclusive staged writer and one directory per adapter;
  each data callback remains bounded to 512 bytes. Upstream file-slot and physical
  capacity limits remain authoritative.
- Multiple bindings can share an admitted provider. Ending one invalidates only
  its contexts and releases only its grant; other bindings retain their provider.
- Source, installed-file and settings bytes are preserved by the composition
  tests. Existing files are not overwritten. This is live-session/error coverage,
  not reboot or power-loss durability qualification.

## Product integration still required

1. Select a genuinely mutable, user-owned storage provider and an existing root.
   For Watch, define how that storage survives app/cohort updates while retaining
   the current SPIFFS mount owner. This code supplies no replacement backend,
   new partition, data migration or permission to reuse private app-data.
2. Admit that ordinary provider and dependencies through the product's existing
   validated provider graph, with exact identity/instance and no implicit native
   privilege. Its interface must supply the existing checked storage extension
   needed by `ScopedUserVolume::configure` and `beginExtended`.
3. Add an explicitly authorized Runtime/app binding that owns this object for
   the full invocation lifetime. Validate and copy its root policy before loading
   providers. Fence retained state before app finalization, foreground switching,
   handoff, provider teardown, sleep and update. End the binding before releasing
   its graph/storage owners. Boot-schema/app-grant plumbing is not implemented here.
4. Select the deployed scoped capability in the existing File Browser build and
   manifest, while preserving installed files as separately authorized read-only
   data. This checkpoint changes no browser source, app manifest or Watch profile.
5. Reconcile actual simultaneous bindings, provider descriptor/file-slot capacity,
   metadata memory and the current Runtime integration branch. This unit does not
   consume a product version or claim a target link.

WebDAV over Wi-Fi still needs a bounded protocol/transport owner, explicitly
selected exported volumes, authentication/session admission, streaming responses,
request cancellation and checked shutdown. BLE discovery/brokering still needs its
service identity and session negotiation. Neither transport, credentials, network
exposure nor automatic service startup is introduced by this binding.

## Reproduction and evidence

```sh
bash test/run_scoped_user_volume_binding_test.sh /path/to/System-Apps /path/to/Reader
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 \
  bash test/run_scoped_user_volume_binding_test.sh /path/to/System-Apps /path/to/Reader
```

The harness builds actual ordinary provider shared objects with the existing v2
descriptor, and the production Graph/Module loader admits, maps, starts, pins,
quiesces and unloads them. The provider fixture forwards the unchanged canonical
Reader FatFs volume on a RAM disk; no host directory is mounted or device touched.
The consumer invokes the unchanged real shared browser `fbx_copy` path.

Dependencies are System Apps `16058588cd8e64021b8c89c4fbad3f2ecc736a5a` and Reader
`3f59913963840cb5ee8e2a65baa921f8d6af0b4d`, supplied as verified local archives.
Twenty-one isolated cases cover provider/root/media/operation absence, exact and
ambiguous instances, copied configuration, wrong owner, unavailable admission,
reentrant lifecycle calls, shared grants, adapter-slot exhaustion, actual full
storage and clean retry, provider release retry, native I/O/custody loss, failed
activation and revoked release custody. Terminal cases preserve retained objects
until process exit; their destructors are deliberately not used as cleanup.

The absent-media case simulates unavailable provider readiness. The full-storage
case fills the actual FatFs RAM volume. Full-media setup uses bounded filler files.
Removing a whole-disk file would
exceed the production provider's per-operation sector budget; the recovery case
instead frees one bounded filler through that same production interface.
The fake fixture descriptor does not constitute Watch firmware or a shipping
native provider. Target builds, hosted CI and physical behavior remain untested.
