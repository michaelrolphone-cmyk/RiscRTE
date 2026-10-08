# Readonly provider realtime dependency

Runtime 0.1.52 exposes `platform.realtime@1` only to ordinary providers that
explicitly declare that dependency. `sdk/driver/RiscPlatformRealtimeV1.h` aliases
`risc_platform_realtime_api_v1` to the canonical `risc_realtime_api_v1`; consumers
also include `sdk/app` for `RiscRealtimeV1.h`. There is exactly one snapshot layout
and validator. The table contains `api_version`, `struct_size`, `context` and
`read(context, risc_realtime_snapshot_v1*)`. It has no seed/write callback.

The snapshot remains UTC Unix epoch seconds/nanoseconds, explicit UNSET/VALID
and boot-local monotonic sample bounds. UNSET means zero epoch/fraction. VALID
means seeded, not authenticated accuracy. Deep-sleep restoration comes from the
existing native backend; this route neither reads raw RTC memory nor estimates
elapsed sleep. Timezone/DST interpretation and persisted user timezone policy
remain with consumers. See [canonical semantics](REALTIME.md).

## Admission and authority

The Runtime registers its readonly broker using the existing registered-platform
registry only when a selected manifest declares the capability. The already
registered canonical realtime backend is required. Unsupported API versions,
missing backend, ambiguous bindings and app grants for `platform.realtime`
fail admission before module execution or native I/O. Generic registration may
not substitute an arbitrary backend/control table for this reserved broker.

Each selected provider receives its own readonly table through the ordinary
graph dependency mechanism. The module's existing host lifetime lease allocates
a fresh, nonreusing opaque token on start and revokes it before diagnostics,
quiesce or stop. Tokens are compared, never dereferenced; exhaustion fails
closed. Copied retired tables cannot revive after reacquisition, another Runtime,
or reuse of the same Runtime address. Providers already using bound KV share the
lease without changing their key map, nine-key limit or KV startup authority.

A read requires the current Runtime's owner, active `app_main`, the provider's
live generation, a safe selected graph and existing native/provider-storage
barriers. Provider start, app init/fini, shutdown, failed graph, pending release,
held resources and retained invocations reject before reaching the backend.
Healthy bounded provider polling during app entry is supported. An observed
native-storage barrier revokes realtime contexts until provider reacquisition;
clearing the barrier alone cannot revive a copied token. Denial or malformed
backend output leaves the caller's snapshot unchanged.

Staged cohort admission copies only the existing native backend descriptor and
rebuilds candidate-owned readonly tables and leases. It does not borrow source
Runtime generation tables or invoke read/seed. Provisioning's ordinary prepare
path uses the same metadata-only registration. No app grant, raw import,
`platform.clock@1` prefix, hardware identity or product behavior is added.
Graphs without the dependency consume no new platform slot or context token.

## Verification

- `test/run_provider_realtime_test.sh`: production Runtime/Graph/Module with
  real dlopen providers/apps; eager/demand, owner/entry/poll, output validation,
  no seed authority, missing backend/API/app-policy denial, per-provider tokens,
  reacquisition, exact-address Runtime reuse, combined nine-key KV, rejection of
  ten keys, native/invocation retention, failed graph/pending release and retained
  shutdown. Normal and ASan/UBSan variants run in CI.
- `test/run_realtime_test.sh`: existing app and native realtime tests plus
  provider denial under actual CpuPort held-pad and retained deep-entry barriers.
  The native SDK shim verifies cold UNSET and seeded fresh-process deep VALID,
  reset classification, elapsed SDK time and unchanged error outputs.
- Cohort tests exercise the readonly dependency with candidate-local tables,
  maximum-sized graphs and no native read/seed or dynamic module execution.
- Existing Watch graph/dynamic-instance tests, bound KV, legacy ABI and lifecycle
  regressions remain separate compatibility checks.

Base: public `602ae9bd618e13407b5b94bcad86cdabc23c99ea` (PR41, Runtime 0.1.51).
Version 0.1.52 was reserved after live open-head version, discussion/comment,
issue/PR and refs/tag checks on 2026-10-07. Exact target compilation is supplied by
hosted CI because the shared local tool directory has the pinned Xtensa compiler
but no PlatformIO platform/framework. No packages are installed for this slice.
Hardware RTC drift, power, wake reliability and physical display behavior remain
unqualified. No product/source-lock/BIN changes, main merge, release or device
operation are included.
