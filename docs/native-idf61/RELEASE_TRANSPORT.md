# Runtime0.4.0 exact native CI release transport

This transport is checked in **disabled**. It builds nothing, does not access a
device, and cannot publish from a local invocation. Only the separately enabled
GitHub-hosted release job uses its job-scoped `GITHUB_TOKEN`.

## Identity and activation

The existing Runtime convention is `firmware-v<version>`; this release is
`firmware-v0.4.0`, titled `Runtime0.4.0 — Native ESP-IDF 6.1`. It is published as a
prerelease, not made latest, because product hardware qualification is pending.

The only request is `docs/native-idf61/runtime-0.4.0-release.json`. An exact push
to `work/native-idf-6.1` changing the manifest or publication implementation runs
`.github/workflows/native-idf61-release.yml`. The workflow first runs offline
negative tests and validates the request; a disabled request skips publication.
There is deliberately no `workflow_dispatch` dependency. Documentation-only
changes do not activate the workflow.

The enabled manifest must be reviewed and supplied only after the final native
build run has finished successfully. Never guess IDs, SHA values, or inventories.
The original qualified `source_sha` is separate from the later publication-only
commit. The release tag targets the original qualified source, not its publisher
successor. Publication-only files may be excluded narrowly from the native build
workflow's paths so a transport request does not cause a redundant firmware build.
The transport does not modify that build workflow itself.

## Active manifest schema (schema 1)

Required top-level fields:

- `schema`: integer `1`; `enabled`: boolean `true`
- `repository`: `michaelrolphone-cmyk/RiscRTE`
- `version`: `0.4.0`; `tag`: `firmware-v0.4.0`
- `publication_branch`: `work/native-idf-6.1`
- `source_sha`: full original green Runtime commit SHA
- `source_workflow_sha256`: SHA256 of that commit's `.github/workflows/native-idf6.yml`
- `run_id`, `run_attempt`, `workflow_id`: exact positive GitHub API IDs/attempt
- `artifacts`: exactly `generic`, `twatch`, `waveshare`, and `x4`

Each artifact record contains exactly:

- `id`: immutable GitHub artifact ID
- `name`: `native-idf6-<product>-<full-source-sha>`
- `bytes`, `sha256`: original downloaded artifact ZIP size and SHA256
- `job_id`, `job_name`: exact successful product matrix job ID and full name
- `sources`: role to `{revision, tree, submodules}` from original clean CI custody
- `members`: every regular ZIP member path to `{bytes, sha256}`

`members` is a complete inventory, not just a selection of desired payloads.
Paths use the original `candidate/`, `build/`, `metadata/`, and `logs/` layout.
The generic artifact has only Runtime and SDK sources; product artifacts also
have product, System, Drivers, and image-helper sources. Shared pins must agree
across products. SDK source and official image digest are fixed by the verifier.
Source locks, run metadata, workflow bytes, before/after clean custody,
qualification records, complete candidate checksums and native output hash locks
are cross-checked. Watch/Wave require `controlled_build`; X4 requires
`build_closure`, its exact SD/ELF inventory, and final-ELF bootlog placement.

These JSON records are evidence from a successful controlled build, not a new
build attestation. Publication trusts the exact successful official workflow,
repository, source, attempt, four jobs and immutable original artifact digests.
It never treats arbitrary detached qualification JSON as CI provenance.

The source file version must be `0.4.0`. Forks, pull-request runs, failed/skipped
jobs, rerun-attempt changes, missing/extra artifacts, expired artifacts, changed
ZIPs, missing members, unsafe paths, links, duplicate names, false hardware or
preserving-migration claims and rebuilt/repacked payloads fail closed.

## Published assets and downstream use

All original ZIPs are published without recompression:

- `Runtime-0.4.0-Generic-Native-IDF61-Evidence.zip`
- `Runtime-0.4.0-Watch-1.1.40-Native-IDF61-Evidence.zip`
- `Runtime-0.4.0-Wave-0.1.49-Native-IDF61-Evidence.zip`
- `Runtime-0.4.0-X4-0.1.122-Native-IDF61-Evidence.zip`

The machine-readable public provenance asset is
`Runtime-0.4.0-release-manifest.json`. Its JSON is canonicalized (sorted keys,
two-space indentation, final newline), so consumers pin the SHA256 of the
published bytes rather than assuming it equals the checked-in formatting.
`SHA256SUMS` covers all other assets, including this manifest and the notice.

The generic app, debug ELF, MAP, bootloader, partition table, and sdkconfig are
also exposed directly under `Runtime-0.4.0-Generic-Native-IDF61-*` names. Generic
`.bin` names explicitly include `NOT-PRODUCT-FLASH`. They are **not device product
flash images** and do not supply the device package/configuration.

Product CI can fetch the public manifest and product ZIP by exact SHA256,
validate their agreement and the complete member inventory, then copy the
qualified assets without rebuilding or relinking. GitHub source ZIP/tarball
assets are not the CI evidence ZIPs. Product release publication remains its own
separately authorized action.

All product factory images **erase data**. Hardware tests remain pending.
Preserving SDK migration is **not yet eligible**: exact deployed bootloader,
candidate boot, health confirmation and rollback need hardware validation.
No preserving-update kit is emitted, and publication never flashes hardware.
X4 retains the exact complete released SD ZIP and device MSC/SD workflow.

## Publication transaction and failures

The job checks for any existing tag or release, including drafts, before
mutation. After downloading and fully verifying all four CI ZIPs, it rechecks
CI identity and absence again, atomically creates a new lightweight tag at the
qualified source, creates its own draft, and uploads the complete asset set.
It re-downloads every uploaded asset and verifies actual bytes before exposing
the draft. It then reads the public release and re-downloads every asset again.
No overwrite, deletion, clobber, or automatic adoption/resume of an existing
release/tag is allowed. A failure can leave a blocked tag or draft requiring
separate owner review; rerunning does not silently repair or overwrite it.

Signed storage downloads never receive the GitHub token. ZIP contents are read
with strict canonical paths and bounded sizes; they are not blindly extracted.

## Local non-publishing checks

Run:

    python3 -B test/test_publish_native_idf61_release.py
    python3 -B scripts/publish_native_idf61_release.py check-manifest

After an enabled exact manifest and local copies of the four original ZIPs are
available, `verify-archives --archives <directory>` validates `generic.zip`,
`twatch.zip`, `waveshare.zip`, and `x4.zip` without network or publication.
`publish` refuses a local context before network access.
