# Removable application images

Runtime 0.2.5 can obtain an application's executable through a selected
`storage.volume@1` provider. Boot policy, manifests, grants and all providers
remain in the internal store. The default application cannot be external.
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
