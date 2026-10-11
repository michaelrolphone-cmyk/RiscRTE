# Opt-in shared Files composition capacity

Base: 91324f41b5a79df21f6bc39a04899eba4d0be351 plus the isolated entropy,
HCI coexistence and scheduler-only wait patches. No async storage v3 or v2
installed-discovery interface is changed.

`RISC_PROVIDER_CAPACITY=32` selects 32 providers and 48 graph grants on cohort
profiles. It preserves the existing sixteen shared foreground/resident grant
slots. With discovery, use `RISC_INSTALLED_APP_CAPACITY=64` as the current X4
composition does. This does not change per-invocation ledgers or app policy
bounds. Default discovery remains 29/45, default cohort remains 28/44 and legacy
remains 17/32 even if the new selection is supplied. Bounds reject shrinking
below the default or exceeding 40. No firmware selection is implicit.

Actual Xtensa C++ object layout: one Runtime grows 264184 -> 276808 bytes
(+12624); two retained metadata Runtimes cost +25248 bytes in PSRAM. CpuPort
adds 240 bytes and the stream registry adds 60 bytes. Runtime already includes
the graph; do not double-count its +3624-byte subobject. Full layout data is in
shared-files-capacity-memory.json. These are exact object sizes, not measured
free heap, execution peak or physical hardware headroom.

Complete current X4 d38300bccdbf75d2d786bd3cbc87101fef1dabb7 discovery store
plus Files and three providers: 26 apps, 32 providers, all 42 internal target
ELFs pass production strict role/import validation and Runtime cohort admission
with zero hardware or storage calls, using the existing native resolver witness.
An updated native source build must explicitly select TCP and entropy; the
existing native BIN is used only as a resolver witness and is not a deliverable
for this composition. No firmware was linked or flashed for this checkpoint.

Reproduce sizes with test/shared_files_capacity_test.py --target-prefix
/path/to/xtensa-esp32s3-elf- --output /new/output. The test also verifies legacy
layout equality. The X4 isolated qualify_shared_files.py runs the real admission
paths with inert native TCP/entropy callbacks; all calls are counted and forbidden.

The generic `esp32s3-16mb-appdata-iq-sharing` environment is an explicit source
build entrypoint with the above 64/32/48 and 24/24 selections plus native TCP and
entropy. It inherits the existing IQ/paired app-data layout; default environments
remain unchanged. The X4 composer independently exposes recorded opt-ins for
provider capacity and sharing transport while preserving its native boot hooks.
The Watch full-canvas source build can use the generic sharing environment.
No selected native firmware is produced by this checkpoint.
