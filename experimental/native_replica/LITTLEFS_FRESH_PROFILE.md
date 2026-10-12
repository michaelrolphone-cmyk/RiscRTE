# Explicit pinned LittleFS artifact profile

This is an experimental, unselected native profile. It does not enable FreshEmpty
creation on ESP, change a product, mount a partition, format storage at boot, or
provide an application endpoint. The unchanged default `CheckedFreshInitArtifacts`
continues to use its strict Linux no-follow/inode/link proof and returns `Io`
before filesystem I/O on ESP and unsupported targets.

## Separate admission and injection

`CheckedLittleFsFreshInitArtifacts` is explicitly constructed with one immutable
`Mount` and one trusted-native `Port`. `FreshInitArtifactInspector` is a narrow
value adapter accepted by the checked store and NVS source collector. Only the
existing Linux/unsupported inspector and the new concrete pinned inspector have
constructors in that adapter; there is no generic success callback constructor.
Old call sites preserve their selected concrete inspector. No preprocessor flag
automatically substitutes the target profile.

The inspector reuses the existing inspector's private canonical encoding and
validators via one friend declaration. RTENR03 FreshEmpty remains exactly 484
bytes, its object exactly 508 bytes, and scratch a permitted 0..508-byte prefix.
The integrated imported path accepts only the current RTENR03 ImportedLegacy
initialization grammar (strictly sorted map, provenance, nonzero fingerprints,
archive obligation, all CRCs). The SD path accepts only the current RTENR01 known
SD initial image (unique opaque map in its original order and exact checked
identity framing). Both paths accept images up to 65,512 bytes and reject every
other version/shape. No sorting, merge policy or source selection is added.

The full bank must equal the supplied validated image with a valid object ID;
`.pending` may only be a prefix of that exact object. Offset-aware canonical
comparison uses a 512-byte stack scratch and checks EOF, including across every
chunk. Both bank names and `.pending` are independently checked against the
complete bounded directory enumeration. Bank B and every unknown, duplicate,
overlong or wrong-type entry refuse. The Linux inspector and its dispatch remain
unchanged except for the friend declaration.

Adapter copies preserve both underlying concrete type and object address.
NVS source collectors and checked SD restore compare that exact identity. The
permanent factory validates the complete concrete object's INTERNAL range,
namespace, full root path and existing PSRAM-only allocator hooks. LittleFS
`boundTo` compares the store root with `mountPath + privateRoot`; callbacks still
receive the VFS-relative private root. An adapter's address never establishes
inspector identity, and no callback or namespace-only match substitutes it.

The port's coherent snapshot must bind:

- Exact pinned VFS profile, captured VFS context and partition object
- Expected aligned partition address, exact 512 KiB size and no encryption
- Exact mount path, provisioned firmware-private root and worker identity
- Nonzero mount, private-root, native-lease, all-writer-fence and VFS-redirection
  epochs, with no wrap/reuse within the lifetime of any borrowed inspector
- The actual native shared lease in held state, zero competing writers and zero
  nested mounts affecting the private root

Every observation/cooperation boundary rechecks the complete immutable tuple.
Filesystem calls use the captured VFS context directly; they never resolve a
new global VFS route. The same all-writer and mount-manager exclusion must also
cover the checked store's POSIX mutation route. The last authority check is a
coherent snapshot, so an observation callback cannot revoke the lease after the
last check and allow an I/O call. A lost tuple with an open handle is sticky
`Retained`; without a handle it is `Busy`. Observed Retained has priority.

The production gate is deliberately still open: `NativeAppData.cpp` currently
provisions only public `/appdata`, while the background resource API provides no
private-root/mount-registration/worker epoch witness. A production port must
establish the private root, capture the actual registration and partition,
exclude remount/nested registration and every native writer/cleanup owner, and
retain that state for the full factory/store operation. Merely supplying these
fields or callbacks does not prove those obligations. No such production adapter
is supplied or selected here.

## What zero metadata means

The pinned VFS actually leaves `st_dev`, `st_ino`, `st_nlink` and `st_ctime` zero.
The new profile requires the zero identity/link/device fields as an exact-profile
sanity check, never as identity evidence. Mode, regular-file size and seconds
mtime comparisons are consistency checks only. Directory size is never validated
or compared: pinned LittleFS documents `lfs_info.size` as valid only for regular
files and leaves it unwritten for directories, while ESP stat copies that field
into `st_size`. This is not stable directory metadata. `fstat` is not used as independent object identity;
the pinned VFS itself resolves its retained path. There is no hard-link callback
and LittleFS defines only regular-file and directory object types. Nonaliasing
therefore relies on the pinned no-link filesystem plus admitted native custody.
No fake inode, link count or no-follow flag is introduced.

An actual fault sweep found that pinned `stat` returns success even if its
separate optional mtime-attribute read fails. The profile clears and captures
`errno`: only zero or `ENODATA` for an absent optional attribute is accepted on
stat success. Other errors refuse. Canonical contents and checked EOF are still
required. An ordinary read cannot prove a global block graph or detect malicious
raw-flash writes; that is outside this profile.

Both file close and directory close are checked. Failed close is never retried,
even if the underlying close actually consumed the handle. Retained/lost-custody
operations issue no later filesystem calls. Destruction does not close retained
handles or release their mount/lease owner.

## Actual-source qualification

Historical FreshEmpty checkpoint: `09e71e5`, repaired as `6c43965`.
Current integration base: `bee53b48d0b8cbd355c40f5a7c87f6e0f4e2cd0d`.
Historical logs under `evidence/littlefs-profile` remain unchanged; integrated
qualification is documented in `LITTLEFS_RESTORE_INTEGRATION.md`. Vendor pins:

- ESP LittleFS `41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317`
- LittleFS `f53a0cc961a8acac85f868b431d2f3e58e447ba3`

The runner checks SHA-256 for all eight vendor inputs and compiles the actual
unchanged `esp_littlefs.c`, `littlefs_esp_part.c`, `lfs.c` and `lfs_util.c`.
No authored filesystem implementation or host POSIX storage stands in for them.
The fixture captures the real registered VFS table/context. SDK/FreeRTOS/newlib
boundary declarations and physical flash are modeled; the target binary ABI and
RTOS are not emulated. See `test/native_littlefs_profile_stubs/README.md`.

The bounded flash array uses real pinned partition callbacks, checked 128-byte
read/program alignment, one-to-zero programming and 4096-byte erase blocks.
The config matches the selected package's relevant LittleFS values, including
512-byte caches, 128-byte lookahead, 512 block cycles and seconds mtime.
The consumer uses the actual `NativeRadioAsync::tryShared/endShared` atomic
implementation and worker-thread identity, not an always-true lease hook.
Test mount/root/redirect epochs model the required future native adapter.

The composed path executes actual `NativeReplicaCore`, `NativeBaselineAdmission`,
`CheckedNvsFreshSources`, `NvsReplicaMetadata`, `CheckedNativeReplicaStore` and
`AppDataFiles` (ESP mode), routed to the actual VFS/LittleFS. Only NVS storage is
an explicit map model; this is not NVS flash or metadata-durability qualification.

Coverage includes:

- 3,054 canonical prefixes, every length for six IDs including high-byte carries
  and UINT64_MAX-1; 2,021 truncation/corruption/source-image/type/sequence refusals
- Empty/missing namespace, repeated missing lookup and root epoch invalidation;
  complete fixed inventory, early enumeration EOF and duplicate rows
- Deterministically differing/negative directory sizes in missing and populated
  inventories; regular-file size changes still refuse
- 1,873 I/O, underlying flash-read and callback failure boundaries, including
  native lease loss, root/mount/lease epoch changes, cancellation and reentry
- Read-only inspection has zero flash program/erase calls and leaves all flash
  bytes unchanged
- Real zero stat/fstat fields, absent link callback, wrong-worker and competing
  native acquisition refusal; actual remount invalidates an old bound profile
- Core/store first creation and reload; all 509 interrupted scratch prefixes
  resume with a newly reserved ID, never promotion of an old object
- Seven composed competing-source/unknown-artifact cases preserve both NVS model
  and flash bytes exactly
- 32 before/after write, sync, close, rename, flash program and erase failures on
  FreshEmpty creation; 31 recover via absent/receipt state after RAM-discard
  remount, 16 originally report retained custody

These counts are reached operation faults, except where a separate probe says
"attempts". The fixture's test-only reset/reboot discards volatile allocations
without close/flush/sync/retry and remounts the existing bytes without formatting.
Explicit test `reset` alone formats fresh model storage. This models loss of RAM,
not physical power interruption or legal runtime release of retained custody.

## Reproduction

```
ESP_LITTLEFS_SOURCE=/path/to/verified/esp/files \
LITTLEFS_SOURCE=/path/to/pinned/littlefs \
python3 test/run_native_littlefs_profile_test.py

ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 \
ESP_LITTLEFS_SOURCE=/path/to/verified/esp/files \
LITTLEFS_SOURCE=/path/to/pinned/littlefs \
python3 test/run_native_littlefs_profile_test.py

test/run_native_fresh_init_artifacts_test.sh
test/run_native_fresh_initialization_test.sh
test/run_native_nvs_fresh_sources_test.sh

test/compile_native_littlefs_profile_target.sh PACKAGES_DIR OUTPUT_DIR
XTENSA_CXX=PACKAGES_DIR/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++ \
test/run_native_fresh_init_artifacts_target_compile.sh
test/compile_native_nvs_fresh_sources_target.sh PACKAGES_DIR OUTPUT_DIR
```

The runner may instead fetch exact pinned public sources into its temporary
build directory; it verifies all hashes before compiling. ASan+UBSan results
are separate from LeakSanitizer, which cannot run under this executor's ptrace.
The existing Linux/unsupported and complete fresh/NVS collector regression
suites also run normally and with UBSan. Target compilation uses pinned Xtensa
GCC 8.4.0; it is compilation evidence only. The original default target inspector
still emits the same normal/ESP unsupported object and no filesystem imports.

The historical target profile was 456 bytes and callback-based typed injection
24 bytes. The integrated inspector remains 456 target bytes. Concrete-only
dispatch uses two concrete pointers (8 target
bytes); the original target inspector remains 184 bytes. Current individual
compiler frames include inventory 752, file inspection 656, authority check 464
and inspect 96 bytes. These frames nest,
include neither callback nor VFS/LittleFS costs and are not an aggregate stack
bound. The inspector itself does not allocate; actual VFS/LittleFS does.

A separate negative large-write probe exposes a vendor assertion after the
second flash-program failure with an 8192-byte file. It is documented and kept
reproducible separately. FreshEmpty's 508-byte successes do not qualify larger
writes, global filesystem corruption recovery, NVS durability, production
mount/writer binding, target execution, physical flash/power loss or hardware.
Larger read-only imported/SD grammar tests do not remove that mutation-path gap.
This integration does not activate a firmware/profile.

## 2026-10-12 sanitizer repair

The original `evidence/littlefs-profile/asan-ubsan.log` is preserved unchanged.
The diagnostic rerun in `asan-ubsan-prefix-repro-20261012.log` identifies pending
object ID 2, prefix length 10, persisted sequence 1, result `Io` (-5). The
stage/stat reruns show identical private-root directory mode and mtime with
`st_size` changing from 0 to 508. This is the pinned directory-size contract
above, exposed by stack layout, rather than a rejected canonical prefix.

The inspector now validates nonnegative size and compares size only for regular
files. It still checks directory type/mtime, the full admission tuple, complete
bounded inventory and canonical file contents. The test retains failure-only
ID/prefix/result diagnostics and adds deterministic directory-size regression
coverage. Vendor bytes and default profile selection are unchanged.

Repaired full-suite evidence is separate in `normal-repaired-20261012.log` and
`asan-ubsan-repaired-20261012.log`; the original failure is not overwritten or
relabelled as a pass. Normal and ASan/UBSan regression logs cover the existing
Linux/unsupported inspector, complete fresh initialization and NVS source
collector. New `*-target-repaired-20261012.log` and target-compile logs are compile
checks, not firmware activation or hardware qualification.
