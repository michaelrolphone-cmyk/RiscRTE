# Bounded-memory historical-page inventory, 2026-10-12

This local increment extends the typed restore at f59fb7f. It changes only the
restore catalog, its focused tests and documentation. Production boot, private
mount policy, native writer/broker authority and the SD format are unchanged.

- Host checked restore: all original cases plus 1/65/257 faithful generations,
  63/64/65 descriptor windows, duplicate/case/source/stability/resource failures,
  changed preflight identities and open/closed-window cancellation/Retained.
- Catalog allocator peak: exactly 176,972 bytes for each tested generation count;
  excludes Core/store/provider/task/product memory. Historical bytes unchanged.
- 154 returned native faults, 209 partial object prefixes and 134 fork/_exit
  process-death cutpoints remain passing. Host and process-death suites pass
  ASan/UBSan with LeakSanitizer disabled under ptrace.
- Actual pinned X4/FatFs: 130 generations, 261 entries, six concurrent coordinator
  cases, normal + ASan/UBSan + TSan. All 249 external inputs verified. The normal
  source manifest is included; sanitizer manifests are under the local outputs.
- Ordinary catalog regression and all 61 actual-X4 consumer cases pass.
- Xtensa 8.4 target compilation passes; catalog object 15,576 bytes. This is not
  firmware linking, hardware, latency, stack or complete-product heap proof.

Temporary full outputs: /tmp/checked-sd-many-final. No physical hardware used.

The original 128-entry/8192-name-byte limits still cover nonhistorical inventory.
Restore-only history uses a 64-entry window and repeated fully closed sweeps.
Limits.restoreWork defaults to and cannot exceed 1,048,576 yielded entries across
both inventories; cutoff preserves everything and returns Conflict. Large-card
I/O cost grows roughly quadratically in entry count divided by window width;
there is no unlimited-history, latency or automatic-GC claim. Private production
mount remains closed. Unknown/malformed, same/newer unselected, backup/staging,
and genuinely competing source cases remain fail-closed as documented.
