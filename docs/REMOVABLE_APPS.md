# Removable application images

Runtime 0.2.5 can obtain an application's executable through a selected
`storage.volume@1` provider. Boot policy, manifests, grants and all providers
remain in the internal store. Without an explicit internal fallback, the default
application cannot be external.
No removable storage is accessed during metadata or internal-cohort admission.

An admitted `app_capabilities` row may include:

```json
"image_source": {
  "volume_instance": 9,
  "path": "Apps/ebook-reader/ebook_reader.elf"
}
```

Paths are normalized volume-relative ELF paths. The instance must resolve
uniquely to an already selected volume. This field grants the native loader
read access to that single image; it does not grant storage access to the app.
Driver choice and version remain platform policy. Apps request capabilities.

Each launch reads a new snapshot of at most 2 MiB with bounded 4 KiB requests,
cooperative checkpoints and a 30-second budget. Positive short reads continue.
The file is closed and its provider grant released before structural validation
and ordinary ELF relocation/import admission. Mutable source images never enter
the immutable internal-image cache. Relocation creates fresh globals and BSS.

Absent, busy, truncated or malformed images fail the child launch and use the
existing default/resident-shell failure path. A failed checked close or provider
release retains the provider and invocation; it never pretends cleanup succeeded.
The loader neither formats storage nor copies executables into internal flash.

Internal inventory and cohort admission include the app's metadata but omit
its external executable explicitly. Device launch validates the actual snapshot;
product packaging must separately qualify every executable shipped on SD against
the matching native firmware. Flash app-update operations reject external apps.

This is the first transition stage: firmware still declares installed app
identities, capability grants and launcher entries. Replacing the corresponding
SD executable does not require copying it into flash, but adding an entirely new
app identity or changing its grants still requires an updated platform policy.

## Boot-owned SD-preferred core images

An immutable boot policy may opt in per application, including the configured
default, by adding `"internal_fallback": true` to `image_source`:

```json
"image_source": {
  "volume_instance": 9,
  "path": "Apps/core/image-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.elf",
  "internal_fallback": true
}
```

The source path is explicit and may be content-addressed by the publisher. The
loader does not infer a hash or identity from its name. The existing internal
manifest supplies the unchanged application identity, version, grants, private
namespaces and internal ELF path. This is a boot-image selection policy, not a
catalog update or an application privilege. Dynamic admission cannot supply the
field, including `false`; dormant catalog authority cannot select it. Omission
keeps existing behavior. An explicit `false` on a boot-owned non-default row
also keeps external-only behavior.

Each launch tries a fresh SD snapshot. Absent, busy, removed, truncated or
malformed media, and ordinary snapshot relocation/import failures, may select
the internal image only after checked close and provider release succeed. The
internal path uses the ordinary fresh-instance ELF loader. Both paths pass normal
loader validation and the same app lifecycle admission. App init/entry failure
does not retry another executable. Neither path grants the app a storage-volume
capability. No file is written, repaired or promoted during loading.

A failed close, lost owner/storage custody, retained graph cleanup or uncertain
provider release stops the invocation. It never triggers fallback, retries an
uncertain close, or releases a live source underneath it. The loader's bounded
read cancellation and cleanup-custody checks are separate, so an ordinary read
failure may close safely while lost custody preserves the source grant.

Internal image inspection, installed-file inventory and full-cohort inventory
include every opted-in fallback ELF. Whole-store provisioning must supply and
admit these complete internal images even when a mirror is available. Metadata
and staged cohort admission perform no SD I/O. Existing bundled-identity dynamic
installation restrictions and partition layouts are unchanged.

`test/run_removable_runtime_test.sh` covers real host default/child mappings,
SD preference, internal fallback, both ELF rejection paths, absent/busy/removal,
retained close/release/custody, strict boot-only policy, unchanged grants and
private namespaces. `test/run_cohort_runtime_test.sh` requires default and
non-default fallback images during complete inventory admission. Host coverage
does not execute Xtensa instructions or qualify physical SD hardware.
