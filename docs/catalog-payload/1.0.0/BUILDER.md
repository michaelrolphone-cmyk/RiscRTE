# Catalog website-builder handoff 1.0.0

Implement the browser installer and the release-level `payload_manifest` adapter
using the frozen [payload contract][payload] and [RTEF1 contract][serial]. The
catalog owns release association, validation, ordering, UI and receipts. Firmware
provides only admitted generic individual-file operations.

## 1. Bind one immutable release

- Add `release.payload_manifest` with [descriptor.schema.json][descriptor]:
  `schema_version`, plus `manifest` and `container`, each containing `asset_name`,
  `bytes` and `sha256`. Resolve those names against the selected release's actual
  assets. Preserve existing fields and revision checks.
- Keep legacy `release.payloads` records readable. Their `sd|spiffs|littlefs`
  targets and phase descriptors are not generic namespace bindings; do not
  silently convert them or route uploads by those labels.
- Freeze the catalog revision, upstream numeric release ID, exact firmware/source
  identity, artifact hashes, payload inventory hash and trusted product/profile
  facts together. Verify both asset digests and lengths, and require the archive's
  `manifest.json` to match the standalone manifest exactly. URLs/tags alone do not
  identify immutable bytes.
- Apply the [manifest schema and semantic validator][validator] before mutation:
  complete required-file inventory, canonical container, source hashes/totals,
  paths/aliases, operations, parents, compatibility, mappings, bounds and flash
  effects. Obtain trust pins from the existing trusted catalog/profile state;
  hashing an untrusted manifest does not establish trust. Unknown versions,
  fields, operations or ambiguous associations fail closed.

Complete examples: [bootstrap manifest][example-manifest], [descriptor][example-descriptor],
[trusted fixture context][example-context] and [full container ZIP][example-zip].
The [five fixture directories][examples] cover bootstrap, both update orders,
read-only final mapping and flat/root-only storage. They use harmless firmware
text and synthetic namespace **7**; `--allow-fixture` is for offline tests only.
Real X4 admission and a usable product payload mapping require their own evidence.

## 2. Resolve admitted generic destinations

For each logical volume, use its explicit trusted `(firmware_id, volume) → namespace`
binding and target-consumer evidence. Enumerate actual admitted IDs/writable flags
with HELLO/NS and match the selected firmware's profile. Namespace IDs are opaque
uint64 values; use decimal strings/BigInt in JavaScript, never a lossy Number.
Labels, mount paths, backend names and provider instance numbers cannot route data.
A listed namespace may still be unavailable; never fall back to another one.

Preserve destination UTF-8 bytes exactly. Apply the linked path rules and the
selected provider's stricter bounds; do not normalize Unicode, case, whitespace
or URL escapes. Do not prepend a physical mount root. A flat provider supports
root-only inventories without MKDIR. A private serial-storage mapping alone does
not prove the target application can consume the uploaded files.

## 3. Own the serial port and follow the wire contract

Use one owner-trusted Web Serial connection at **115200, 8 data bits, no parity,
1 stop bit**, with competing readers/writers stopped. Keep the device at a safe
foreground service checkpoint. Coordinate the existing browser console, serial
installer and flasher through one port owner; drain/finish their work and release
reader/writer locks before closing or handing over the port.

Implement the exact [requests/responses/statuses][operations], [framing/bounds][bounds]
and [recovery rules][recovery]. Use one in-flight request, bounded parsing and a
finite deadline that diagnostic lines cannot extend. Begin with a fresh nonzero
session and HELLO at sequence 1, enumerate NS, and increment sequences without
wrap. Keep paths/chunks/offsets within the published limits. Do not invent commands
for firmware identity, free space, directory listing, flash or reboot; obtain
identity and supported flash behavior through the existing catalog/product flow.

## 4. Preflight, transfer and verify each file

1. Before the transfer stage, STAT every declared destination and parent; reject
   known conflicts. Recheck after flashing and immediately before mutation.
   Respect trusted provider limits/write budget; RTEF1 has no free-space query,
   and a static budget does not guarantee live capacity.
2. Process the complete declared parent list ancestor-first. Existing directories
   pass; existing files fail. Only authoritative NOT_FOUND permits single-directory
   MKDIR. BUSY, UNAVAILABLE, I/O error and retained custody are not absence.
3. Honor each file's exact `create` or `replace` choice. Create requires absence;
   replace requires an explicitly selected existing regular file. Check supplied
   `previous` ownership/size/hash guards before PUT; omitted guards add no new
   prerequisites. Never change mode automatically on a conflict.
4. Select the namespace-relative path with PATH; issue PUT in the declared mode,
   WRITE validated immutable bytes, then COMMIT with IEEE CRC32. Advance offsets,
   CRC and progress only by acknowledged byte counts; continue legal short I/O,
   reject zero progress and require successful sync/close acknowledgment.
5. Read the entire file back with GET/READ/CLOSE, checking exact length, transfer
   CRC32 and host-computed full SHA-256 against the manifest. Empty files still
   require PUT/COMMIT and GET/CLOSE. CRC32 detects corruption; it is not authentication.

Modify only listed individual files and necessary listed parents. Preserve
unrelated files and directories; never replace, delete or format whole folders.
Replacement truncates the selected file at PUT and is **destructive and non-atomic**.

## 5. Choose flash order and hand over safely

Select a supported flash method from declared artifacts, destination effects and
consumer mapping. Show its actual data/settings erase effects before starting.
The current catalog flash command admits full initial images and explicitly
replaces saved data; it supplies no generic preserving-update method.

- `firmware → payload → verify`: supports initial factory bootstrap with accurate
  erase disclosure and transfer after flash. Confirm the exact target firmware is
  running, reopen the serial port, start a fresh session and rediscover its mapping
  before file mutation.
- `payload → firmware → verify`: requires the installed firmware's transfer mapping
  and explicit preservation evidence for every required destination across that
  exact method/transition. Unknown or erased destinations reject this order before
  mutation. Verify transfers and close healthy sessions before flash handover;
  afterward reopen, reconfirm firmware/mapping and read back every required file.
  The final mapping may be read-only when STAT/GET and the target consumer suffice.

Finish and acknowledge healthy transfers before handing the port to the supported
flash/reboot flow. RTEF1 has no reset command; an open transfer fences normal
restart/USB handover. Reboot only at the supported product-flow point, then verify
running firmware and final payload usability. Port closure or elapsed time does
not prove device cleanup. OS/USB line changes on open/close need hardware testing.

A paired update succeeds only when its entire required payload is verified and
usable by the target consumer after the last firmware step. If no supported
method/order can achieve that result, keep the combined update unavailable.
Disclose settings effects separately; there is no universal settings-survival claim.

## 6. Report progress and recover explicitly

Show selected release/firmware, complete file count/bytes, per-file destinations
and create/replace choices, flash order and erase warning. Track download validation,
flash, transfer, readback and final usability separately. Report acknowledged bytes
and verified files, not bytes merely queued to the browser writer.

For a definitive transfer error, follow the pinned client's known-handle ABORT
rules. For timeout/disconnect, malformed or mismatched replies, uncertain offsets,
−6 or −10, stop and mark the session unusable. Send no cleanup request and do not
automatically retry, reconnect, reset or overwrite. A lost PUT ACK may mean
truncation; a lost COMMIT ACK may mean completion. Inspect custody and the selected
destination before an explicit recovery transfer. Readback may establish success;
a partial create needs a reconciled replacement choice. ABORT cannot undo writes,
restore a replaced file, remove a partial create or roll back earlier files.

Save per-file receipts with release/manifest/container hashes, firmware identity,
namespace/path, operation, expected/actual bytes and hashes, stage and acknowledged
outcome. A later failure leaves the release incomplete with any prior effects
reported accurately. Provider completion does not promise power-loss durability.

## 7. Run conformance and accept the browser implementation

From a checkout of Runtime commit
`908355b0509907c05a6dc47a1ccc8e078d6e23b0` (Python 3.10+, Bash, C++17 compiler):

```sh
python3 -m pip install -r docs/catalog-payload/1.0.0/requirements.txt
python3 docs/catalog-payload/1.0.0/test_validate.py
for name in bootstrap update-after update-before update-before-readonly flat-root; do
  python3 docs/catalog-payload/1.0.0/validate.py \
    "docs/catalog-payload/1.0.0/examples/$name/container.zip" \
    --context "docs/catalog-payload/1.0.0/examples/$name/context.json" --allow-fixture
done
python3 docs/catalog-serial/1.0.0/verify_freeze.py
bash test/run_serial_files_test.sh
bash test/run_serial_files_client_test.sh
```

The [payload tests][payload-tests] exercise 48 positive/adversarial cases. The
[wire tests][wire-tests] and [client tests][client-tests] contain executable
request/reply vectors, short I/O, corrupt/missing replies and custody faults.
Reuse these outcomes when testing the browser port; passing the reference tests
alone does not qualify the browser implementation.

- [ ] Legacy records stay readable; mismatched identities, hashes, inventory,
      paths, aliases and unsupported mappings fail before mutation.
- [ ] Binary, empty, short-I/O, parent, create-conflict and replace cases match
      the reference behavior; sibling files/directories stay unchanged.
- [ ] Only an explicit admitted mapping routes files; missing/unavailable/read-only
      destinations and provider bounds are handled without fallback.
- [ ] Both orders, flash/serial ownership, reboot and final readback are exercised;
      transfer-first rejects unknown/erased effects and permits valid read-only finals.
- [ ] Lost PUT/COMMIT ACKs, disconnects, CRC/SHA mismatches, zero progress and
      retained custody produce accurate incomplete receipts and explicit recovery.
- [ ] Progress, non-atomic replacement and actual erase disclosures match observed
      effects; final success requires the target consumer to use every required file.

Record source, software tests, publication and hardware results separately. The
frozen Wave/T-Watch releases remain **hardware untested**; the synthetic fixtures
are offline test data. Browser integration and actual X4 bootstrap admission are
separate deliverables. Roll out publication and installation in this order: X4
initial bootstrap, its paired firmware/payload update, Waveshare, then T-Watch.
Keep the existing hardware qualification gates between those stages.

[payload]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/README.md
[serial]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/891c4a9b20cc03fb1e6899ad287e9e22da9f64de/docs/catalog-serial/1.0.0/README.md
[descriptor]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/descriptor.schema.json
[validator]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/validate.py
[example-manifest]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/examples/bootstrap/manifest.json
[example-descriptor]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/examples/bootstrap/descriptor.json
[example-context]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/examples/bootstrap/context.json
[example-zip]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/examples/bootstrap/container.zip
[examples]: https://github.com/michaelrolphone-cmyk/RiscRTE/tree/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/examples
[operations]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/891c4a9b20cc03fb1e6899ad287e9e22da9f64de/docs/catalog-serial/1.0.0/README.md#4-exact-operations
[bounds]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/891c4a9b20cc03fb1e6899ad287e9e22da9f64de/docs/catalog-serial/1.0.0/README.md#3-bounds-and-framing
[recovery]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/891c4a9b20cc03fb1e6899ad287e9e22da9f64de/docs/catalog-serial/1.0.0/README.md#5-byte-accounting-integrity-and-recovery
[payload-tests]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/docs/catalog-payload/1.0.0/test_validate.py
[wire-tests]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/test/serial_files_test.cpp
[client-tests]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/908355b0509907c05a6dc47a1ccc8e078d6e23b0/test/serial_files_client_test.py
