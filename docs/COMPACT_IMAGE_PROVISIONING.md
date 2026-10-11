# Compact-image provisioning (Runtime 0.1.70)

Schema 3 installs a complete, immutable SPIFFS image from one owner-pinned HTTPS
URL. The owner profile also contains the exact required file inventory, including
`board.json`, `boot.json`, every selected driver/application manifest and ELF,
and `default.elf`. No application or provider runs until native admission has
validated the whole downloaded store and selected an inactive pair.

This mode supports dense stores whose final packed filesystem fits but whose
incremental append/index garbage exceeds streamed-file capacity. Schema 1 and 2
keep their existing download behavior and conservative write-demand guard.
Their rejection must never be bypassed to produce an image profile.

## Capacity and immutable storage

An image must exactly match the selected ABI1 or ABI2 store partition size.
Before mounting, native code independently verifies its complete SHA-256, the
pinned 256/4096-byte SPIFFS geometry and length-bound magic in every block.
It counts actual live and deleted lookup entries; deleted pages consume space.
Free lookup entries must reference erased data pages and form each block's tail.
Unused lookup bytes must be erased. The image must have at least four completely
free blocks and no more than `(blocks - 4) * 15` occupied pages.
Live pages must also have matching identities and valid flags. Object headers
must describe a nonempty regular file and terminate their name within 32 bytes;
this prevents the pinned filesystem's unbounded name copies from being reached
with malformed headers.

The pinned filesystem can repair one bad-magic block at mount, so bad magic is
refused before mount. Mount never formats. Verification opens only read handles
and checks exact inventory, each length/hash, every referenced ELF/import, board
and dependency graph, and the exact native/cohort identity. The filesystem image
must remain byte-identical after mount/admission and after independent paired
transaction readback. No `.provision-sha256` file is added to an image.

The existing paired journal carries the private profile's consumption receipt.
Readiness, receipt and selector ordering, app-health confirmation, rollback,
retained-close behavior and uncertain-selection refusal remain unchanged.
Interrupted or failed downloads affect only the inactive store; a later clean
attempt starts by reusing the existing verified inactive clone. NVS and app-data
are never download destinations. The existing five-minute transaction deadline,
bounded HTTP reads and cooperative checkpoints still apply.

The exact accepted Watch 1.0.12 is a capacity regression fixture: 91 files,
4,780,155 payload bytes, 19,318 live packed pages, no deleted pages and eight
completely free blocks. Streaming those files charges 20,103 pages against a
19,380-page limit, and pinned SPIFFS actually returns FULL before completion.
Increasing the write buffer cannot solve this: even one append per file charges
19,549 pages. The original accepted native 0.1.55 does not support schema 3.
Deployment with this extension needs a separately bound product cohort; the
accepted release bytes and version are never relabeled.

## Public inventory and private owner input

First admit the complete product store against the exact native ELF and pack
the filesystem using the product's existing verified image builder. Supply the
actual immutable published image URL explicitly. From a clean Runtime checkout:

```sh
python scripts/provision_profile.py image-inventory \
  --store "$PRODUCT_STORE" --image "$PRODUCT_BOOTFS" \
  --layout riscrte-paired-appdata-v2 --target esp32s3-16mb-appdata-iq \
  --image-url "$IMMUTABLE_IMAGE_URL" --output "$NEW_PUBLIC_DISTRIBUTION"
```

This checks physical occupancy and independently decodes the image against every
local store byte. It emits `image.bin`, public inventory version 2, checksums and
a completion marker. It does not download, publish, flash, or infer package
dependencies. Image occupancy/decoding does not replace product graph admission.

The existing `profile` command accepts the published inventory and emits owner
schema 3 automatically. Keep real Wi-Fi credentials and all generated private
outputs outside Git and publication directories:

```sh
bash scripts/build_provision_input_tool.sh "$VALIDATOR"
python scripts/provision_profile.py profile \
  --inventory "$PUBLIC_INVENTORY" --wifi-file "$PRIVATE_WIFI_FILE" \
  --validator "$VALIDATOR" --time-server "$OWNER_TIME_SERVER" \
  --output "$NEW_PRIVATE_OWNER_DIRECTORY"
```

Do not supply `--base-url` for image mode: its one image URL is already pinned.
Profile schema 3 contains only `schema`, `schema_version`, `wifi`, `image` and
`files`. The image has `url`, `bytes`, `sha256`; every inventory file has `path`,
`bytes`, `sha256`. Mixed fields, unknown keys, duplicate paths/JSON fields,
noncanonical HTTPS URLs and oversized profiles fail closed. The maximum remains
128 files and 16 KiB of private profile input.

Use the ordinary `provision_seed.py` and `provision_device.py` first-install
procedure with the exact supporting native candidate. The private composer
requires `RISC_PROVISION_IMAGE:1` in both native BIN and ELF, preventing an image
profile from silently pairing with an older seed. Native provisioning still
clones the running firmware and requires the product cohort's exact matching
native version/length/hash. A firmware with merely the same version is not enough.

The composed full image remains initial-install-only: it replaces NVS and
initializes app-data. It must never be used to update an existing device.

## Verification

`test/run_store_image_capacity_test.sh` exercises native/Python parity, geometry,
corrupt lookup/magic/free pages, exact bounds and short/error reads. The pinned
SPIFFS image harness verifies production `StoreFiles::verifyImage`, repeat/remount,
inventory/hash/refusal and close failures without any HAL write or erase.
Its admission callback is counted but does not execute a product graph.

`test/run_native_bank_test.sh` runs the actual bootstrap downloader, native image
writer, independent raw readback, graph/ELF admission, paired journal and selector
against controlled network/flash/filesystem adapters. It covers irregular
chunks, interruption/retry, write/readback failure, corrupt image/metadata,
retained cleanup, receipt errors and ambiguous selection; active firmware/store,
NVS and app-data remain unchanged. Target builds and actual product admission
are additional gates. Host results do not qualify Wi-Fi/TLS, physical flash
power cuts or product execution on hardware.
