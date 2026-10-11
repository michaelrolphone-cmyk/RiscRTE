# Peer Files X4 boot retention correction

Base: Runtime `209274bb65a3bc90d0e6a8f5d69199263c1bc7b6`, delivered with
X4 `7394ea165deb5fbb568b6993fb7cf6fab766ef24` (0.1.83). This is a focused
source correction, not a release or a replacement for the integrator's newer
composition. No capability ABI, selector, capacity, package, or export-map change.

The reported hardware log reaches Home and successfully starts installed-apps,
s3-radio-iq and contexts, then reports the native retention barrier. Runtime's
acquisition path used the full app-exit idle barrier. A copied storage-worker
request is safe to keep running inside an invocation but is deliberately unsafe
at actual exit. Home's installed-app scan can therefore make a later acquisition
irreversibly retain the application and providers. The production Runtime/CPU
port/storage-worker regression reproduces this on the exact base source.

Acquisition and provider promotion now check native custody safety. Actual app
exit, restart and sleep retain their idle requirements. Faults still revoke
authority and retain images without cleanup retries. Native AppData exposes its
existing backend custody predicate separately from its strict exit predicate.

The separate repeated SD initialization comes from RemovableAppImage calling
refresh before every provider read. X4 refresh enters init_card, closes the
controller, power cycles with 80 ms and 120 ms waits, then initializes/mounts the
card. The reader now refreshes only when the volume reports not ready. Individual
volume operations still reject unavailable, exported, removed or failed media.

Validation (macOS, UBSan; not a hardware qualification):

- New storage_work_acquire production-path test: base reproduces retention;
  corrected source acquires during pending copied work and exits after collection.
  Actual exit with pending work and injected native custody failure still retain.
- Native AppData tests distinguish pending-work custody from exit idleness and
  reject a retained async writer in both checks.
- Removable image tests count refresh calls: none for repeated ready reads, one
  for an unready card; existing truncation, timeout, allocation and close fences pass.
- Storage worker port, provider exit lifecycle, native AppData and removable
  Runtime suites pass, including actual module loading, fallback, unavailable
  media, exported/busy states, remove/replace, catalog authority and retained close.

Reproduction: copy the three storage_work_acquire test files to the base checkout
and run `EXPECT_OLD_FAILURE=1 SANITIZE=1 bash test/run_storage_work_acquire_test.sh`.
On corrected source omit EXPECT_OLD_FAILURE. These use a mapped app and production
Runtime, CPU port and copied worker; hardware callbacks are controlled doubles.
The test establishes the source defect matching the log, not an observed repaired
hardware boot. No serial access or flashing was performed.

## Diagnostic build receipt

Built Runtime correction `ef7d68718d4f054447ecd79cf008549723eb532b` with
X4 `7394ea165deb5fbb568b6993fb7cf6fab766ef24`, using the same 64-app/32-provider,
24 policy/requirement rows, image cache, USB PHY, IQ, retained-wake and failure
recording options as delivered. Single-job ESP32-S3 compilation passed.
The complete 26-app/32-provider store passed strict ELF/import and Runtime
cohort admission with no hardware/storage calls. SD manifests and linked native
export-map V2 snapshots were verified. Full layout and bank hashes were parsed
and checked independently after assembly.

- Native firmware SHA256: `9ae270ccda4c1072dc28b4c0ea6c9f2f510de833e2eed1913da594f46e20393d`
- 16 MiB full image SHA256: `8ae6e8a53011323325ec9197c7368e9e01d9ab8569d07e55f4d8ef4314ad3d86`
- Diagnostic archive: `X4-PEER-BOOT-FIX-ef7d687-DIAGNOSTIC.zip`, with ELF/map,
  SD contents, patch and detailed receipts. The initial full image replaces data;
  it is not a preserving update. No new product release/version is claimed.

The broader resident-shell suite asserts at `resident_shell_test.cpp:359`
(`childFiles.dir_next`). It fails identically on the unchanged 209274b base and
on this correction under the same macOS harness. This is not reported as a pass.
Hardware boot/live heap and Watch hardware validation remain outstanding.
The sole integrator should apply the focused source commit to current source;
this diagnostic baseline must not replace newer composition work.
