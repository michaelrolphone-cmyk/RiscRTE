# Checked NVS FreshEmpty source inventory

Experimental, disconnected from production routing and product selection.
This increment adds a read-only source collector for the existing native NVS
layout. It replaces synthetic legacy-source facts for this particular admission;
it does not implement imported-baseline materialization or select an SD dataset.

`CheckedNvsFreshSources` binds one namespace, a copied stable 16-byte native-owner
domain, the checked private-artifact inspector and a worker/global-lease guard.
It checks `rte%08x`, `kvj%08x` and `kvd%08x` in read-only mode. Any occupied key,
including unknown keys or legacy journals/anchors, refuses FreshEmpty admission.
No legacy values are read, interpreted, erased, exported or logged. Empty native
namespaces and unrelated settings namespaces do not block verified absence.

The private `rnr%08x` namespace must be empty for creation. Receipt recovery
enumerates its complete metadata, accepting only an exact `RTENFI1` root and
optional valid monotonic `RTENSEQ`. Unknown/type-mismatched/duplicate entries,
missing enumerated records, malformed records and changed entry counts refuse.
NVS's iterator API is used according to its contract: a null `entry_next` means
end of enumeration. The API has no separate next-error status. A null initial
iterator while the native used-entry count is nonzero refuses. This is not an
independent verification of NVS's underlying flash implementation.

The 32-byte FreshAbsenceV1 witness is an exact injective encoding:

- bytes 0..7: `RTENFV1` plus NUL
- bytes 8..11: bound namespace, little endian
- bytes 12..15: source-inventory schema 1, little endian
- bytes 16..31: stable firmware-native owner domain

All inventoried variable source facts must be checked absent; only exact receipt-
owned initialization artifacts normalize to that original absence. The witness
contains no pointer, clock, boot epoch, card generation, random value or credential.
No entropy or hashing callback is required. A different owner/schema must retain
the old validator if it wants to recover an old receipt; it cannot relabel one.

Artifact validation uses the existing checked inspector, including canonical
484-byte FreshEmpty image and 508-byte object/prefix rules. Metadata and competing
legacy namespaces are checked again after inspection. The actual native lease
must exclude all competing writers/cleanup for the whole operation. An observed
retained state or lease loss with open NVS handles is sticky; no subsequent NVS
call or destructor cleanup releases uncertain custody.

## Qualification

Run `test/run_native_nvs_fresh_sources_test.sh`, also with `SANITIZE=1` and
`SANITIZERS=undefined` or `address,undefined`. The focused suite composes the
collector with the actual admission factory, checked store and AppDataFiles,
using deterministic host NVS calls and the existing checked Linux inspector.

- creation and receipt resume with no SD or entropy
- 12 competing legacy row/journal/anchor/delta/unknown-key refusals
- 8 owner/metadata/type/iterator/source/artifact refusals
- all 509 canonical interrupted scratch prefixes, reserving new object IDs
- 88 reached read-only NVS failure points, no metadata mutation or handle leaks
- callback reentry, wrong task, unavailable NVS and late-retention precedence
- sticky custody with an open native handle; simulated reset is the only release

Normal, UBSan and ASan+UBSan are required for the final source. The sanitizer
runner uses leak detection disabled where ptrace prevents LeakSanitizer;
that is not a LeakSanitizer pass. NVS iterator allocation belongs to its adapter;
the collector itself owns no heap allocation. Host object size is 80 bytes;
pinned Xtensa GCC 8.4 object size is 52 bytes. These are not total task-stack,
filesystem, firmware residency or hardware measurements.

`test/compile_native_nvs_fresh_sources_target.sh PACKAGES OUTPUT` compiles the
complete collector against pinned ESP-IDF 4.4.7 NVS headers with ESP_PLATFORM,
warnings as errors, exceptions/RTTI disabled.

The target private-filesystem inspector remains deliberately unsupported.
Production still needs qualified exact VFS/partition/private-root binding,
the actual global worker lease, native routing, archival legacy migration,
SD attempt/lifecycle binding, audited writers and hardware qualification. No
boot flag, existing settings or Watch policy changes in this increment.
