# LittleFS repair integration comparison

Compared repaired worktree (base `09e71e5b9c7937f1a0a9102680adac8acad89f48`)
with Runtime `bee53b48d0b8cbd355c40f5a7c87f6e0f4e2cd0d` using read-only Git
objects. The target worktree was not edited. A textual merge is not a verified
integration. The narrow FreshEmpty repair is tested on its saved source base;
the newer SD/import/factory composition is not tested by this checkpoint.

## Per-file result

The read-only three-way merge reports textual conflicts at the private canonical
constants/friend insertion in `CheckedFreshInitArtifacts.h` and at the private
collector state in `CheckedNvsFreshSources.h`. `CheckedNativeReplicaStore.h`
merges textually, but its downstream typed composition still requires repair.
Full three-way diagnostics are in `integration-three-way-20261012.log`.

- `CheckedFreshInitArtifacts.h`: this branch adds only a friend declaration.
  Main adds exact `boundTo(ns, root)`, imported and SD entry points, bounded
  chunked reads, and extends `Canonical` with image bytes/payload. Main's prefix
  helper now takes `(bytes, offset, length, canonical, next)`. This branch's
  LittleFS call has the old four-argument form; adapt it with explicit offset 0
  for its bounded 508-byte FreshEmpty grammar. Preserve all main methods and
  checks. The existing canonical FreshEmpty generator still has the same v3
  form; no need to replace main's broader inspector with this old copy.
- `CheckedNativeReplicaStore.h`: this branch changes the included inspector,
  constructor argument and stored pointer to a typed adapter value. Main adds
  ImportedLegacy/SD receipt and custody paths, namespace identity, matching
  staged/root revision checks, three-path cleanup and private checked-restore
  access. Retain those paths and all newer factory binding/allocator checks.
  Main's `CheckedSdNativeRestore::configured()` compares `store_.fresh_` with
  `&sources_.artifacts_`; that expression is not compatible with two by-value
  adapters. Preserve exact underlying inspector identity plus concrete type,
  not adapter-address equality, namespace-only equality or a success callback.
- `CheckedNvsFreshSources.h`: this branch changes concrete reference storage to
  a typed adapter value. Main's `boundTo(ns, concreteInspector)` currently
  compares the concrete inspector address; preserve that exact identity
  guarantee using adapter-aware identity. Main also adds private `SdCheck`,
  SD receipt/source comparison and `inspectSdAttempt` dispatch. The saved
  adapter has no SD method, so a textual merge cannot compile as-is. Preserve
  the full collector and factory composition checks rather than transplanting
  this older header wholesale.
- `FreshInitArtifactInspector.h`: new on this branch, absent in `bee53b48`.
  It forwards namespace, retained/exit safety, absent and FreshEmpty attempts
  only. It does not expose root binding, concrete identity, imported attempts
  or SD attempts. Those omissions are semantic integration blockers despite
  any textually clean merge. Existing Linux inspector dispatch must remain
  unchanged. Unsupported operations for the LittleFS FreshEmpty-only profile
  must refuse explicitly until independently implemented and qualified.

The parent also identified newer factory-only `boundTo` accessors and payload
allocator composition checks that must survive integration. They must be
preserved from the actual current integration tree. The exact four-file state
at `bee53b48` does not contain those later store/factory additions; do not assume
this comparison authorizes overwriting or removing them.

## Small standalone integration path

1. Add the separate repaired LittleFS inspector and replay fixture without any
   production selector or firmware flag change. Add only the friend declaration
   to main's existing canonical checker, and adapt the offset-aware prefix call.
2. Reconcile the typed adapter against the current factory/store/collector API.
   Keep exact namespace/root/inspector identity and existing allocator binding
   checks. Forward imported/SD inspection to the original Linux inspector; keep
   those operations fail-closed for LittleFS until their own grammar and fault
   coverage exist. Do not relax existing checks to make the adapter compile.
3. A distinct default-off production port must derive actual VFS registration,
   partition, private-root custody, worker identity, lease and writer/mount/
   redirect epochs from trusted native owners. Bind store POSIX mutations to
   that same retained owner. The existing fixture's synthetic epochs are test
   inputs, not a production witness or authorization.
4. Qualify the composed production port and replay latest fresh/import/SD,
   factory wrong-root/wrong-inspector/wrong-allocator, retained-custody and
   default-off target tests before selection. The 508-byte FreshEmpty result
   cannot qualify larger SD/import images. The separate 8192-byte vendor
   program-failure assertion remains an explicitly unqualified mutation path.

This checkpoint supplies no production mount adapter and activates no firmware.
Runtime.28 first-boot AppDataFiles mkdir/replace diagnosis is a separate task;
it was not changed or investigated here.
