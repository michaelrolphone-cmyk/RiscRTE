# Provider-bound app-data files

`storage.app-data.bound@1` is a provider-only view of the real compiled-in
`AppDataBackend`. It adds no application schema, storage implementation,
partition, mount, capacity reservation or migration. A backend must supply
stat/read/replace and its nonmutating exit-safety barrier. A target without that
backend refuses the dependency during metadata admission, before loading code.

## Explicit policy

A selected boot driver declares `storage.app-data.bound` API 1 in its manifest's
`requires`, and has an exact `app_data` list in `boot.json`:

```json
{
  "manifest": "service/manifest.json",
  "app_data": [
    {"name": "catalog.json", "namespace": 5, "access": "read"},
    {"name": "state.bin", "namespace": 5, "access": "read-write"}
  ]
}
```

The list has 1–4 entries. Each entry has exactly `name`, `namespace`, and
`access`. Names are 1–48 ASCII characters, first alphanumeric and subsequent
characters alphanumeric, `.`, `_`, or `-`. Names are case-sensitive and distinct
within one provider's map, even if their namespaces differ. Namespaces are
integers 1–2147483647. Access is exactly `read` or `read-write`. Empty, null,
malformed, duplicate, undeclared, wildcard, alias and unsupported-version maps
fail admission. This reserved capability cannot be provided by an ELF, granted
to an app, or routed through a hardware binding.

Metadata admission allocates only the actual bounded maps. It does not stat,
read, replace, create, mount or format app-data files. First provisioning uses
the same `Runtime::prepare` checks against the actual target backend; declaring
policy does not create data. Filename/content initialization belongs to the
explicitly authorized provider or owner provisioning process.

## ABI, authority and storage results

`RiscBoundAppDataV1.h` defines `risc_bound_app_data_v1` as the exact
`risc_app_data_v1` layout. API version, size, context, stat, read and replace
callbacks are copied exactly. The operations retain the complete
[RiscAppDataV1 contract](../sdk/app/RiscAppDataV1.h), including output rules,
empty files, probes, stale revisions, atomic replacement, uncertain commits and
backend quotas. Result constants use the existing `RISC_APP_DATA_*` names.

The Runtime maps a caller's exact filename to the policy namespace and forwards
that same filename. Callers never provide a namespace or native path. Unknown
names and read-only replacement return `CONTEXT` without backend I/O. Invalid
names/arguments return `INVALID`. Different providers can have the same filename
in different namespaces. App-data and KV namespace numbers belong to separate
stores; their numeric equality never grants cross-store authority. Existing
app-data grants still authorize their entire explicit namespace, while provider
bindings authorize only their exact files. Existing KV policies are unchanged.

Provider start/active calls are valid on the Runtime owner task without a
foreground app. The copied callbacks are generation-bound; release, failed
start, revocation, native unsafe state or Runtime replacement cannot resurrect
old contexts. Revocation precedes diagnostics, quiesce and stop. A bound-file
`RETAINED` result propagates unchanged and latches the whole Runtime invocation.
It blocks later I/O, graph polling/service, app teardown and provider cleanup,
even if the backend's exit-safe flag later clears. The internal host lease has
an optional safety barrier across every provider in a file-enabled graph, so
retention through a dependency is checked after start, diagnostics, quiesce and
stop before any further callbacks or disposal. No provider ABI field changes.

The current backend retains the existing 64 KiB file, four committed files and
128 KiB committed namespace limits; physical shared free space can cause earlier
`NO_SPACE`. Bindings neither increase those limits nor reinterpret bytes as
application entries. Storage medium changes require a separately supplied,
authorized backend; this feature does not migrate data to SD.

## Cohort identity

Full-cohort admission preserves every old app-data namespace and its app
identity, and every old provider file's name, namespace, access and provider
identity/instance. A newly introduced app or provider cannot take a namespace
already used by any prior file principal. An existing provider may add another
explicit filename in a namespace it already owns, subject to the map bound.
New namespaces are admitted through the existing explicit policy path. Existing
KV migration exceptions cannot authorize file namespace reassignment. A fresh
owner-authored boot policy may explicitly grant both an app and provider access
to the same namespace; an update cannot infer that permission from legacy KV.

## Verification

`test/run_bound_app_data_test.sh` loads real Runtime, graph, module and dynamic
provider/application images against production `AppDataFiles`. It covers exact
read/replace and read-only authority, unknown/invalid files, distinct namespaces,
app-grant separation, zero-I/O metadata admission, absent/incomplete backends,
file quota and size limits, revisions/probes/stale writes, owner checks,
release/reacquire, Runtime replacement and failed-start revocation. Isolated
processes inject retained stat/read/replace and unsafe backend reset during
eager provider start and foreground use, proving no later storage/diagnostic/
teardown callback. Poll/service fault tests stop graph callbacks, and an
indirect dependency fixture faults during start, diagnostics, quiesce and stop.
Run normally and with `SANITIZE=1` for ASan/UBSan. If LeakSanitizer cannot operate
under a traced executor, use `ASAN_OPTIONS=detect_leaks=0` and report leak
verification separately.

`test/run_cohort_runtime_test.sh` includes file maps in complete 20/18 and 24/24
cohorts, including namespace theft, policy removal/change and exact expansion
checks; all data backend callbacks abort if metadata admission invokes them.
Target build checks do not qualify hardware, filesystem durability under power
loss, or product data initialization.
