# Durable export marker: experimental core WIP

This checkpoint is core/format-only, based on Runtime
`86f949fe97fb5776549ff1948451778ef5b39deb`. The later adoption-plan documentation
has no implementation difference. It is intended for a separate experimental
branch. The transport, verified-reader admission, and held-rebase capacity
consumers still need their dynamic-header integration before using v2 images.
Do not enable durable preparation through the prior checked transport or claim
that this checkpoint proves end-to-end recovery. No production routing changes.

## Compatibility and capacity

`RTENR01` remains readable and is the default for initialize and ordinary apply.
It has a 128-byte header and needs neither SD nor an entropy service. Native root
records (48 bytes), NVS sequence records, and checked native bank capacity are
unchanged. Explicit successful `prepareExport` upgrades the selected complete
image to `RTENR02`, with a 384-byte header. It costs exactly 256 additional bytes;
an image that cannot accommodate the upgrade is rejected without publication.
With the checked bank's 65,512-byte image ceiling, map plus pending groups have
65,124 bytes in v2, including neither header nor final four-byte CRC.

`core.headerSize()` and `formatVersion()` return selected values when ready and
zero otherwise; `Snapshot.headerSize()` reports its captured envelope. Consumers
must use the actual header for offsets and admission. An uninitialized reader may
use the v1 minimum for a map-only import; explicit v2 admission must reserve 384.
Validation rejects unknown image versions, inconsistent sizes, reserved fields,
invalid native incarnation, marker/prefix mismatch, and bad checksums. Old v1
images are never silently discarded or rewritten on load.

## Native authority and lifetime

A v2 native incarnation is 16 nonzero bytes supplied by the native owner during
explicit v1 export preparation. The caller must supply fresh, collision-resistant
material for each new native lifetime, including after a full native erase. Do
not derive it from clocks, SD lineage, repeated constants, or numeric generations.
Ordinary settings availability is independent of that service. Existing v2
incarnations are preserved, and a different supplied incarnation is refused.
This freshness condition is separate from content hashes and SD lineage checks;
it is not authentication and does not make an untrusted SD authoritative.

`prepareExport(snapshot,target,newIncarnation,out)` requires an exact live capture:
same core owner, current ticket, native revision, sequence, base, and every image
byte, including original pending IDs/intents/operations. The target must be the
known source lineage's immediate next generation. The trusted transport computes
its digest from the captured complete map. Preparation atomically selects the
new root with the marker before any SD publication is allowed.

The fixed 256-byte marker occupies bytes 128..383:

| Offset | Field |
| --- | --- |
| 0 | `RTENEX01` magic |
| 8, 12 | namespace and encoding version |
| 16 | native incarnation (16 bytes; also image bytes 112..127) |
| 32, 40, 48 | captured revision, through sequence, previous acknowledged sequence |
| 56, 60 | captured pending group count and exact encoded bytes |
| 64, 68 | captured map byte size and entry count |
| 72, 136 | exact previous and proposed target identities (64 bytes each) |
| 200, 204 | captured prefix CRC and map CRC |
| 208..251 | zero reserved bytes |
| 252 | marker CRC |

Identity includes known state, generation, lineage and full digest. Equal or
higher generation alone proves nothing. CRC detects corruption; it is not a MAC.
The root-coupled slot avoids a sidecar transaction or a pinned inactive bank.
Ordinary apply retains the marker and captured group bytes while appending newer
edits. Rebase, volatile acknowledgement, and a different export are fenced.

`preparedExport` returns an opaque copied handle only from the selected root;
there is no public decoder/import setter. A handle from an inactive bank, another
native lifetime, or a retired attempt cannot authorize a later transition.

## Recovery caller contract

Under one exclusive same-medium lease, the caller must quiesce all prior export
writers, freshly validate the complete SD candidate and exact identity, and finish
its checked close. Keep this custody through the checked native commit:

- Exact proposed target: `acknowledgeExport` preserves the complete current map,
  removes only the originally captured prefix, advances base/ack, and clears the
  marker in one root replacement. Newer pending groups remain byte-for-byte.
- Exact previous base: `retireExport` clears only the marker, acknowledges nothing,
  and preserves all current values/intents. Recapture the current map afterward.
- Missing, corrupt, foreign, or any other identity: preserve the marker and every
  pending group. Missing SD is never evidence that an attempt was unpublished.

Recovery requires no captured old map in RAM. Cancellation cannot clear a durable
marker. A selected marker survives reboot and arbitrary ordinary newer edits.
Uncertain publication requires reload before use; Retained requires preservation
of resource custody. Neither return code permits speculative acknowledgement.
Initial dataset creation, unknown-base adoption, and legacy migration need separate
reviewed provenance/vacancy authority and are outside this implementation.

## Reproduce the core-only checks

Run from the Runtime worktree:

```
bash test/run_native_replica_export_marker_test.sh
SANITIZE=1 SANITIZERS=undefined bash test/run_native_replica_export_marker_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address bash test/run_native_replica_export_marker_test.sh
bash test/run_native_replica_core_test.sh
SANITIZE=1 bash test/run_native_replica_core_test.sh
bash test/compile_native_export_marker_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR
```

The dedicated suite covers v1 load/upgrade, exact 256-byte capacity overhead,
allocation refusal, stale full captures, original prefix membership, newer edits,
reboot acknowledgement/retirement, inactive-handle rejection, 162 fault outcomes,
9 retained-root outcomes, and 32 malformed images with outer checksums repaired.
The target check compiles every marker API as strict C++17 with exceptions/RTTI
disabled. Host and target compilation are not hardware or production qualification.
