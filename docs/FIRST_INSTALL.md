# Offline first-install packaging

This workflow prepares a new 16 MiB ESP32-S3 device image plus the exact private
provisioning JSON it will consume. It does not connect to a device or execute a
flash command. A separate owner-controlled hardware installation is required.
Host validation is not hardware qualification. Use the pinned Python 3.11 test
and packaging environment. The unmodified IDF4.4.7 NVS generator imports
`distutils`; newer Python environments need a compatible setuptools installation
as well as the existing esptool/pyelftools dependencies. Confirm those tool
prerequisites before adding private inputs.

On boot the generic, verified paired Runtime seed reads the owner profile from
NVS. With Wi-Fi, a fresh owner-selected SNTP sample, and reachable trusted HTTPS
sources, it downloads the complete pinned product inventory into its inactive
bank: `boot.json`, fetched `board.json`, `default.elf`, every selected driver and
manifest, and all additional application files. Production native admission
checks the whole board/configuration/provider/app graph before selection. The
next boot runs the product's selected default application and normal health /
rollback lifecycle. A failed download preserves the generic fallback. Repeated
failed-profile behavior follows [PROVISIONING.md](PROVISIONING.md).

The generic heartbeat seed is deliberately independent of a specific Watch or
e-ink product. Product behavior comes from that product's complete admitted
store. This is not a package dependency resolver: never pass an app-only subset.

## 1. Prepare the public Runtime seed

Use a clean, committed checkout, the pinned target toolchain, and the existing
candidate builder. For ABI1 the existing commands remain:

```sh
pio run -e esp32s3-16mb-paired -j 1
python scripts/paired_candidate.py --source-sha "$(git rev-parse HEAD)"
pio run -e esp32s3-16mb-paired -t buildfs -j 1
python scripts/provision_seed.py \
  --candidate dist/esp32s3-16mb-paired \
  --store .pio/build/esp32s3-16mb-paired/spiffs.bin \
  --cc "$XTENSA_CC" --mkspiffs "$MKSPIFFS" \
  --source-sha "$(git rev-parse HEAD)" --output dist/paired-seed
```

For the incompatible ABI2 app-data layout, first prepare the exact target
LittleFS empty image and include it in the candidate:

```sh
python scripts/app_data_image.py --littlefs-source "$PINNED_LITTLEFS_SOURCE" \
  --output dist/initial-appdata
pio run -e esp32s3-16mb-appdata -j 1
python scripts/paired_candidate.py --source-sha "$(git rev-parse HEAD)" \
  --app-data --app-data-image dist/initial-appdata
pio run -e esp32s3-16mb-appdata -t buildfs -j 1
python scripts/provision_seed.py \
  --candidate dist/esp32s3-16mb-appdata \
  --store .pio/build/esp32s3-16mb-appdata/spiffs.bin \
  --cc "$XTENSA_CC" --mkspiffs "$MKSPIFFS" \
  --source-sha "$(git rev-parse HEAD)" --output dist/appdata-seed
```

The complete current Watch store additionally requires the opt-in IQ target.
For that product, use `esp32s3-16mb-appdata-iq` in the build, buildfs, candidate
and store paths above, and add `--radio-iq` to `paired_candidate.py`. The seed
composer reruns `radio_iq_proof.prove` on the linked firmware ELF and checks the
packaged `radio-iq-proof.json`; the reserved IQ window checks are retained. The
standard app-data target does not admit the Watch `s3-radio-iq` provider.

The seed composer derives geometry from the verified candidate. ABI1 retains
`bootfs0` at `0x310000`, with no app-data image. ABI2 uses `bootfs0` at `0x2f0000`
and verified empty `appdata` at `0x270000`. Both bind bank0 metadata to the exact
Runtime/store bytes, initialize OTA selection and leave inactive bank1 erased.
The public seed contains no NVS or owner credentials. Never mix these layouts.

## 2. Freeze the complete product distribution

Use the product's freshly built, natively admitted store directory. Do not use a
live-device snapshot: it may contain private data. Verify that the product's
board mapping, Runtime ABI and application/provider dependencies match the chosen
device and Runtime version before preparing the distribution.

```sh
python scripts/provision_profile.py inventory \
  --store "$VERIFIED_PRODUCT_STORE" \
  --layout riscrte-paired-appdata-v2 --target esp32s3-16mb-appdata-iq \
  --base-url "$VERIFIED_PRODUCT_HTTPS_DIRECTORY" \
  --output dist/product-provisioning
```

The output contains a frozen `files/` tree, `inventory.json`, `SHA256SUMS` and a
final `COMPLETE` marker. The inventory explicitly pins every file's path, byte
length, SHA-256 and exact HTTPS URL, including `board.json`. It also binds the required
Runtime target to its exact layout; choose the matching target for other
products. A standard app-data seed cannot be composed with an IQ inventory. It refuses missing
required files, symlinks, reserved/prefix-colliding paths, unsupported native
SPIFFS names and capacity violations. Native SPIFFS relative names are at most
30 bytes because the leading slash and terminator count toward its 32-byte limit.
Capacity mirrors the native pinned SPIFFS geometry and 8192-byte append batches:
for each payload and the 32-byte profile digest, charge data pages, final/index
update pages and worst-case append/mtime garbage; reserve four whole blocks.
The native implementation still checks every actual write and readback and
falls back on failure. Passing preflight is not a physical durability guarantee.

For the Watch distribution, the authorized product workflow commits this
credential-free `files/` tree in its separate product branch/PR and records a
verified `raw.githubusercontent.com` base URL containing the exact full commit
SHA. Use that returned immutable URL as `VERIFIED_PRODUCT_HTTPS_DIRECTORY`;
never substitute a moving branch URL or guess a commit/path. The owner only
needs to supply Wi-Fi and the time server privately. No external hosting account
is needed. A specific published URL is deliberately not fabricated in this guide.

Other products likewise publish the contents of `files/` at a verified immutable
base URL using their authorized publication workflow. This script does not upload, perform
network requests, verify that an endpoint is live, or invent download locations.
A release ZIP or whole-store `.bin` alone is not an HTTP source for the individual
profile entries. There is no automatic ZIP extraction on the device. Keep the
inventory with the distribution and verify public content against its pins.

For different source hosts, replace `--base-url` with `--sources`, naming a JSON
object that maps every store path to its exact already-selected HTTPS URL. The
source map must include the full inventory. Every source uses the production
profile URL restrictions: no ports, userinfo, escapes, queries or fragments.
Hashes establish byte integrity/custody, not publisher authentication.

## 3. Prepare private owner input

Use the inventory pinned to the verified public payload commit. No local payload
download is needed for this step: the device fetches every file automatically.
The optional `--store` argument adds a local full-byte comparison for producers
or reviewers who already have the complete store. outside Git

Build the existing production parser/input packager:

```sh
bash scripts/build_provision_input_tool.sh /tmp/provision-input
```

In a private directory outside every Git working tree, create `wifi.json` with
exactly two strings, `ssid` and `password`. Supply actual owner values only in
that private file. The password may be empty for an open network. Never put
credentials on the command line, in the example files, CI, or public artifacts.

```sh
python scripts/provision_profile.py profile \
  --inventory dist/product-provisioning/inventory.json \
  --wifi-file "$OWNER_PRIVATE_DIRECTORY/wifi.json" \
  --validator /tmp/provision-input \
  --base-url "$VERIFIED_PRODUCT_HTTPS_DIRECTORY" \
  --time-server "$OWNER_SNTP_SERVER" \
  --output "$OWNER_PRIVATE_DIRECTORY/new-profile"
```

This verifies every local file against the full pinned inventory. It emits
schema 2 when `--base-url` is given; that URL must reproduce every pinned source.
Schema 2 stores the common URL prefix once and supports the full Watch-sized
inventory within the existing 16 KiB profile-input bound. Omit `--base-url` to
emit legacy schema 1 with explicit per-file URLs, for example for distinct hosts.
Both schemas allow at most 128 explicit files and retain the byte-size bounds.
A schema 1 profile that exceeds 16 KiB must be shortened or use schema 2; it is
never silently truncated. Older Runtime builds without schema 2 support must
continue using schema 1.

The production C++ parser validates both the generated profile and the explicit
time server. Fresh time input is mandatory for this first-install workflow,
because an owner profile alone cannot bootstrap certificate-validating HTTPS.
SNTP is unauthenticated; its limits remain as documented in PROVISIONING.md.

The owner-only output contains `profile.json`, a custody receipt, production
`inputs/` with exact profile/descriptor/time blobs and NVS CSV, and a completion
marker. Profiles, NVS CSV/hex and hashes are private owner material. Encoding is
not encryption. This tool rejects private output inside Git working trees,
including ignored `build/` and `dist/` directories.

## 4. Compose the private new-device image

Obtain the official Espressif NVS generator from
[ESP-IDF v4.4.7](https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py).
The composer requires its exact SHA-256:
`c6979797dcf373b6e0f2700c6d7b54675f3f197619e61325acd8865ca72182b3`.
Use a Python environment with its official dependencies (`cryptography`, and
`setuptools` if that Python needs it for `distutils`). The generator must already
be present locally; the composer performs no download or software installation.

```sh
python scripts/provision_device.py \
  --seed dist/appdata-seed \
  --owner "$OWNER_PRIVATE_DIRECTORY/new-profile" \
  --validator /tmp/provision-input \
  --nvs-generator "$OFFICIAL_NVS_GENERATOR" \
  --new-device \
  --output "$OWNER_PRIVATE_DIRECTORY/new-first-install"
```

Use `dist/paired-seed` for ABI1 and a matching ABI1 product inventory. The composer
rechecks the seed candidate's source/ABI markers, hashes, exact partitions,
pinned rollback bootloader and linked native proof; ABI2 additionally rechecks
the exact initial LittleFS image. It verifies bank journal and OTA-data binding,
regenerates private input through the production parser, invokes the pinned
Espressif generator for a fresh 24 KiB NVS partition, and independently checks
its page/entry/data CRCs and every namespace/blob against the exact owner input.
No unrelated NVS keys are included.

Output includes separate first-install segments with explicit offsets, a full
16 MiB `first-install.bin`, exact `profile.json`, `first-install.json`, checksums
and a final completion marker. The whole output directory is owner-only; files
are mode 0600. Keep all of it local and outside artifact upload directories.

The full image overwrites NVS and, for ABI2, app-data. `--new-device` is an
explicit acknowledgement of this packaging scope, not authorization to access
or flash hardware. Never use this image on an existing device. Existing devices
continue using paired updates and the separate owner-input maintenance installer
that preserves unrelated NVS. This workflow does not provide migration.

## Failure, interruption and verification

Existing output paths/symlinks are refused. Validation precedes output creation
where possible. Caught failures and interruptions remove only the new partial
output. Abrupt termination or power loss may leave a partial directory: never
consume anything missing its final marker/checksums, and revalidate its contents.
No atomic host-filesystem power-loss guarantee is claimed.

```sh
python test/provision_profile_test.py -v
MKSPIFFS="$MKSPIFFS" python test/provision_seed_test.py -v
NVS_GENERATOR="$OFFICIAL_NVS_GENERATOR" python test/provision_device_test.py -v
```

Tests cover ABI1/ABI2 geometry, actual SPIFFS extraction when its official tool
is supplied, 83-file compact profiles through the production parser, complete
inventory and pinned byte refusal, deterministic packaging, no-overwrite,
private permissions/location, failure/interruption cleanup, and actual pinned
Espressif NVS generation with independent multipage-blob readback when supplied.
Modeled candidate/ELF fixtures are identified in tests. These checks do not
execute target product ELFs, establish endpoint availability or qualify Wi-Fi,
TLS, board drivers, app health, flash power loss or physical hardware.
