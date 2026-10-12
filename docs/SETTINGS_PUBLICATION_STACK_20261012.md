# Native settings publication stack repair

The frozen `dc0ba0e` selected Xtensa link contains a feasible **8,512-byte
nested ENTRY-frame lower bound**, exceeding its unchanged 8,192-byte native
worker stack. The route is fresh-initialization resume with a canonical existing
`replica-a.bin` or `.pending` artifact: worker → bootstrap → admitted
initialization → core publication → checked seal → fresh artifact inventory →
file validation → yield/recheck → snapshot custody. The linked compiler inlines
`FreshInitArtifactInspector::inspectAttempt` in this route; no extra frame is
counted for it. `check(true)` calls `check(false)` while its own 544-byte frame is
still live. The JSON frame receipt is in
`evidence/native-settings-native-link-20261012/stack-lower-bound-before.json`.
This is static proof of insufficient capacity, not measured hardware high-water.

`NativeReplicaCore::publish` previously kept a 4,096-byte verification array in
its frame even while nested seal/artifact inspection was executing. The repair
uses an explicit allocator-owned `PublicationScratch`, bounded by
`min(imageBytes, 4096)`. The selected native payload allocator requires checked
PSRAM over the whole raw allocation and has no INTERNAL fallback. These copied
verification bytes contain no atomic, ISR/cache-off, task-control, or DMA state.

Scratch is obtained before backend reserve/write/seal. Checked fresh/import/SD
initialization obtains it before the begin hook can persist an admission receipt.
Failure returns `NoMemory`, frees the uncommitted candidate image, and retains the
existing durable records. The synchronous scratch lifetime is identical to the
old local array's lifetime; every exit wipes and frees it, including retained or
uncertain synchronous returns. No borrowed output is made permanent. The native
worker stack size, IQ reservation, ABI, and atomic/control placement are unchanged.

Focused core tests fail the *second* allocation independently of image allocation
for apply, acknowledgement, candidate installation, and compatibility creation;
assert no backend mutation and unchanged data/revisions; and verify successful
scratch is wiped before release. Fresh initialization tests cover all four owned
image/verification/stage/cache failure positions, including no NVS records for
image or verification OOM. Exact small fresh peak is 1,984 payload bytes rather
than 1,500, with four allocations rather than three. Import tests sweep all shared
allocation positions and recovery paths.

Target frame remeasurement and the combined FileSource/settings link are recorded
in the final resource report; successful host tests alone do not qualify a target
stack, live heap budget, or physical high-water.
