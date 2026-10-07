# Provider exit characterization at Runtime 0.1.50

Source base: `7e79a8f06c1ee3b71214408c2477d9aaca985834`.
Historical characterization commit: `ef9aa35`. Run the commands below at that
commit to reproduce the old observations; its successor changes the runner to
assert the corrected [invocation retention contract](../docs/INVOCATION_RETENTION.md).
`run_provider_exit_lifecycle_test.sh` loads actual host app/provider images using
production Runtime, GraphV2 and ModuleV2. The GPIO variant also uses production
CpuPort; only its lowest hardware callbacks are modeled. No production behavior
is changed by the characterization commit.

## Reproduced ownership boundaries

- Operation false with a healthy provider: app-local retention skips its fini
  cleanup, but automatic grant revocation safely stops providers and unloads
  the app. A conservative bool error is not evidence of Runtime retention.
- Failed start with successful rollback: identical safe handoff, no retained
  graph. Failed acquisition alone must not be redefined as terminal.
- Failed release whose second quiesce succeeds: Runtime retries after fini and
  unloads. An app-local flag does not forbid that automatic retry.
- Native barrier false: no fini, provider cleanup, app unload or child launch.
- Persistent demand-provider failure: automatic revocation retains app and
  providers, but app fini and a quiesce retry have already run.
- Failed demand acquisition with failed start/quiescence: the provider and its
  dependencies remain mapped, but the app unloads, queued child runs, and default
  reloads before final graph shutdown finally marks Runtime retained.
- Eager provider-local terminal failure: boot references suppress last-consumer
  quiescence. App unload and queued child execute before shutdown discovers the
  failed provider. Real CpuPort GPIO-write false reproduces this separation:
  `appExitSafe()` and `providerStorageSafe()` stay true; `quiescent()` is false.
  The fixture models provider-local retention after a write failure, not the
  complete X4 panel protocol. No app-pointer use-after-free is claimed.

The app image's destructor, child entry, provider teardown trace and `dladdr`
checks establish mapping/cleanup custody. Host tests do not establish Xtensa
allocation handling or physical sleep/power behavior.

## Reproduction

Normal characterization checks pass at the source base:

```
bash test/run_provider_exit_lifecycle_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_provider_exit_lifecycle_test.sh
```

The stronger required pre-fini/no-unload/no-handoff condition fails at the base
with exit134 for both of these cases:

```
EXPECT_PROVIDER_EXIT_FENCED=1 PROVIDER_EXIT_SCENARIOS=start-retained bash test/run_provider_exit_lifecycle_test.sh
EXPECT_PROVIDER_EXIT_FENCED=1 PROVIDER_EXIT_SCENARIOS=cpu-gpio-retained-eager bash test/run_provider_exit_lifecycle_test.sh
```

All nine characterization cases pass with GCC14 and ASan/UBSan. LeakSanitizer
must be disabled in this ptrace executor; its initial run fails before leak
results, and is not counted as a pass. Existing demand-activation and native
retained-app suites also pass unchanged.

## Smallest justified separation

Runtime should automatically apply the existing nonmutating graph safety check
before fini/unload, catching Failed modules and pending releases without changing
successful rollback or explicit in-app cleanup retry. Runtime cannot infer
provider-local terminal state from an opaque capability's bool callback.

The existing append-only `risc_runtime_api_v1` is the narrow place for a one-way,
owner-only `bool (*retain_invocation)(void)` signal. It needs no new capability,
selector, cleanup request or hardware authority. Acceptance must revoke app and
provider storage authority, clear queued handoff and pin existing allocations,
images and grant custody without cleanup or polling; the app then returns.
Neither retained-wake persistence nor provider promotion is such a signal.
