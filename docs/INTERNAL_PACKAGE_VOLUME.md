# Opt-in internal package volume

This source checkpoint extends the published SD-first planner (770fcc9f) with a
live, confined file backend. It does not activate the feature in existing images.
The delivered X4 .111 native image remains unchanged.

Select `RISC_NATIVE_PACKAGE_VOLUME=1` together with `RISC_PAIRED_APP_DATA` and
`RISC_NATIVE_APP_DATA_MOUNT=1`. The existing mounted app-data partition and writer
task are reused; no formatting, new partition or worker task is introduced.
`platform.package-volume` exposes only `/appdata/.packages` through logical
`/Apps`, `/Drivers`, and `/AppData` paths. Mount custody excludes unrelated
settings/file-source access, and pending transactions fence lifecycle handoffs.
The fixed copied mailbox consumes internal RAM and must be included in the
combined image's resource qualification before release.

Boot must explicitly select the System `internal-packages` provider and add
`installation.internal_volume_instance: 0`. The existing SD instance remains
`installation.volume_instance`. The tagged installation-media suffix accepts
protected catalog media selectors (0 SD, 1 internal). Public descriptors cannot
supply these selectors. Software volume instance zero is supported by both
application and driver loaders. Legacy installation calls retain SD behavior.

The download-v2 suffix supports a copied free-byte observation (operation 109).
It is not a reservation. Internal space excludes 139264 bytes of catalog/metadata
headroom. Allocation failures retain the journal's medium; they never migrate an
in-flight package to a different filesystem. Ordinary native admission still
checks image hashes, ELF architecture/imports and the complete dependency graph.

Validation: real POSIX file tests, actual host application/driver loader tests
with internal volume zero, and ESP32-S3 compilation of mount, worker and backend.
System tests cover 180 restart cuts with real internal files. These tests model
native HTTP/admission boundaries; combined firmware linking, real LittleFS power
cuts for this adapter, RAM/stack budget and physical sleep/wake remain required
before shipping. The default-off configuration preserves existing activation.
