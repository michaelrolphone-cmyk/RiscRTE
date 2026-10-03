# Integration and firmware publication

This repository follows Reader's source-version, verified-artifact and
GitHub-token publication model, narrowed to one headless firmware product.
Reference: Reader `3d9bc4f373679f5ae8dd184db6a8d0afa5a40231`,
`.github/workflows/platformio-build.yml`, `.github/workflows/release.yml`,
`scripts/verify_release_plan.py`, `scripts/publish_updated_packages.py`.
The current Reader workflow is authoritative over its older release-request docs.

`RiscRTE integration` runs on PRs targeting main/master, manual dispatch, and
as a reusable workflow for releases. It checks out the exact PR head; checks
firmware/app versions; runs real host shared-module integration, board mappings,
dependency/lifecycle regressions and release-negative tests; builds ESP32-S3
firmware, actual Xtensa default/child/provider ELFs and SPIFFS; and verifies a
checksummed candidate bundle before uploading it. Target builds use `-j 1`.
Host/target failures prevent artifact publication and release publication.

Tools are pinned in requirements-ci.txt and platformio.ini. Tool/package caches
are keyed by OS/architecture and those exact inputs, with no broad fallback key.
Compiled path prefixes and SOURCE_DATE_EPOCH derive from the source commit;
release.json records source SHA, tracked-input fingerprint, tool versions and
image layout. SPIFFS is extracted during staging to prove its actual file bytes.

`Publish RiscRTE firmware` runs automatically on pushes to main/master and by
manual dispatch. Publication is allowed only from the repository's current
default branch (currently main); no branch is renamed. A manual feature-branch
run has no publication authority. The workflow runs integration first, downloads
that run's exact-source assets, revalidates them, and publishes a **new** firmware
version from `[riscrte] version` in platformio.ini as `firmware-v<version>`.

An existing unchanged version is a no-op; changed release inputs under the same
version fail and require a version increase. Published assets are never clobbered.
A release is first created as a draft with the full asset set, downloaded and
reverified, then made public. A failed upload/verification leaves a draft and a
failed run; no incomplete public release is reported. A complete identical draft
can be retried. Higher existing versions reject older candidates. Heartbeat app
changes also require its independent manifest/health-line version increase.

The only publication credential is GitHub Actions' built-in GITHUB_TOKEN with
contents:write in the publish job. PR and build jobs retain contents:read and do
not persist checkout credentials. Repository policy must permit the built-in
write token; otherwise publication fails explicitly. No external secret or
unattended device-flashing job is installed by the release workflow.

For version 0.1.0 the artifact `riscrte-firmware-<source-sha>` contains:

- riscrte_esp32s3-baseline_0.1.0-app.bin (application at 0x10000)
- riscrte_esp32s3-baseline_0.1.0-merged.bin (NEW 8MiB deployment at 0x0)
- riscrte_esp32s3-baseline_0.1.0.elf (debug symbols, not flashable)
- riscrte_esp32s3-baseline_0.1.0-bootloader.bin
- riscrte_esp32s3-baseline_0.1.0-partitions.bin
- riscrte_esp32s3-baseline_0.1.0-bootfs.bin (0x310000)
- default.elf, default.json, boot.json, board.json
- release.json, SHA256SUMS

The merged image contains the partition table and boot filesystem. Publishing it
is not permission to overwrite an existing board's layout or user data. The
separate real-CAM PR heartbeat gate is coordinated with the existing trusted
hardware executor; hardware unavailability/failure must be terminal **failure**,
never fake green, silent skip or a manual-unblock status. No required-check rules
or branch protections are created. The owner remains free to merge a failed PR.

## Application-only CAM check

`cam-hardware-build.yml` runs separately on PRs/manual dispatch. It builds
`cam-ci` with 16MiB qio_opi flash/PSRAM settings and an immutable read-only `/bootfs`
VFS containing separately compiled default.elf, board.json and boot.json. The
normal ELF loader still reads/validates/relocates that app. NVS initialization is
wrapped out for this lab candidate. No bootloader, partition table, filesystem
image or NVS data is included in `cam-app-candidate-<full-source-SHA>`.

The five members are firmware.bin, default.elf, board.json, boot.json and
manifest.json. Manifest schema1 binds source SHA, Actions run ID/attempt, target
cam-nosd, embedded-readonly backend, flash_bytes16777216, memory_typeqio_opi,
firmware offset65536/size/SHA256 and payload array file/size/SHA256/image_offset.
The verifier checks each payload at its actual offset inside the firmware bytes
and enforces sector-rounded application size <=0x1f0000. Compiled RTE_SOURCE
must match the candidate commit; stage requires a clean source checkout.

The existing sole hardware controller must independently verify Actions/archive
provenance, the approved CAM MAC28:84:85:4b:a1:1c and pinned partition digest,
then own its normal device locks and application-only flash/cleanup lifecycle.
The cloud check polls exact-SHA status `ESP32-CAM hardware / heartbeat cleanup`.
Absent execution, pending beyond ten minutes, transport error or failed cleanup
ends as **failure**. A previous SHA's result is never consulted. The controller
adapter is managed in Reader's CI work, not installed by this repository.

Release publishing remains a separately verified baseline-artifact operation;
it does not dispatch a new lab flash on main or create branch protection rules.
Published bytes are not a hardware qualification assertion. Existing PR hardware
results remain visible and the owner can decide whether to merge a failed PR.
