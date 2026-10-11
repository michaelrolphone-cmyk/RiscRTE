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
