# Explicit cold-boot provider start

Runtime 0.1.78 accepts the optional exact field `"boot_start": "cold"` on a
selected driver in `boot.json`, with global `"provider_activation": "demand"`
or `"demand-retained"`. This is owner-provisioned startup policy. It does not
grant an app capability or create a dependency between unrelated providers.

```json
{
  "provider_activation": "demand-retained",
  "drivers": [
    {"manifest": "drivers/storage.json", "instance_id": 7, "boot_start": "cold"}
  ]
}
```

This fragment illustrates only the new selection field; a complete boot profile
still requires its existing board and default-app fields. Any selected ordinary
provider may opt in. Runtime contains no product, storage, provider-ID or pin
special case. Omitted fields preserve existing behavior. Null, booleans, numbers,
arrays, objects, alternate strings and the field under eager/omitted activation
reject during admission. The selected manifest/instance remains authoritative.

## Boot classification and admission

`Port::coldBoot` is an optional compiled-in, read-only classification callback.
It must not call Runtime, providers or diagnostics. Runtime calls it once at
`run()` entry if at least one driver opts in, after the owner/reentry/retention
guards and before provider activation. A missing callback rejects execution
before any module is loaded. Profiles without an opted-in driver never call it.

The ESP32-S3 implementation classifies every reset other than
`ESP_RST_DEEPSLEEP` as cold: power-on, software restart, watchdog, panic,
brownout, external, SDIO and unknown resets. Stale SDK wake metadata does not
override the reset reason. Every genuine deep wake, including timer, GPIO and
other causes, remains demand-only. Classification never reads, consumes or
repairs an app RTC checkpoint. A missing, stale, foreign or corrupt checkpoint
does not turn a deep wake into a cold boot. Light sleep resumes the existing
session and does not run the startup pass again.

`prepare()` still validates the entire board, selected manifests/configuration,
hardware ownership, provider storage policy, dependencies/cycles and app policy
before registering the graph. It neither calls the classifier nor starts a
provider. `inspectImages()` and staged-cohort admission still inspect all
selected images, including deferred nodes, without evaluating boot class or
activating code. No unused-image preflight or loader shortcut is added.

## Session custody and failure

On cold boot, each opted-in selected node receives one existing Runtime-owned
graph grant before the first app is loaded. Acquisition uses its exact admitted
ID, capability, API and instance. The ordinary graph loads dependencies first,
establishes provider leases, and uses the existing loader and ABI validation.
Unrelated selected providers remain deferred. No app policy is needed merely to
start the opted-in node, and apps still cannot acquire it without their own
ordinary explicit authority.

The grant occupies its existing per-driver boot slot for the session. It
survives app-grant release and default/child/default handoffs. Demand-retained
promotion recognizes the populated slot, avoiding duplicate session grants;
later authorized app acquisitions share the same mapping. The option does not
arm retention for any other unused node. Existing reverse-slot shutdown and
dependency consumer counts govern cleanup. There is no new grant pool, recurring
callback, background worker or retry loop.

A cold-start load/start failure prevents default-app launch, like an ordinary
required eager provider. Clean failure follows existing checked graph cleanup.
Failed quiescence or uncertain native state retains successful pins, mapped code
and dependencies and revokes provider authority; Runtime does not force unload,
silently continue, retry cleanup past the retention barrier or launch another
app. A successful start is also checked for native/graph safety before app entry.
This check applies to the new opt-in path; existing eager behavior is unchanged.

For a diagnostic storage composition, absence of removable media must be a
healthy started-provider state if the application should continue. Storage
errors and unsafe close/cleanup retain their provider's existing semantics.
The startup grant retains code; it does not authorize skipping the provider's
hardware sleep preparation or keeping media mounted through sleep.

## Diagnostic composition and limits

An opted-in diagnostic storage provider can copy/export the bounded current and
historical native snapshots during its initial usable mount, without opening a
file browser or waiting for sleep. Runtime only starts the admitted provider;
snapshot selection, media mounting, export bounds and persistence remain external
provider/native policy. This option adds no later diagnostic polling/export
callback. Events after that initial export, including later terminal failures,
may remain in native persistent history until the next successful cold-boot
export. A deep timer wake does not automatically load storage just to export.

## Verification

`test/run_cold_provider_start_test.sh` uses production Runtime/Graph/Module code
and real host shared modules. It covers strict policy parsing; cold start with
no app grant; dependency-first loading; unrelated-node deferral; demand and
demand-retained behavior; deep-wake zero-start/promotion; ordinary later acquire;
handoff/promotion/mapping reuse; owner and missing-classifier denial; complete
metadata/image admission; clean/partial failed starts; failed-quiescence custody;
and native retention before app entry. The classifier is sampled once only at
execution. `test/cohort_runtime_test.cpp` admits and inspects an opted-in staged
cohort with a classifier that must never be called.

`test/native_retained_wake_test.cpp` checks the production native classifier
against all reset classes, deep timer/GPIO/other wakes, stale timer metadata on
non-deep resets, and deep wakes without an app checkpoint. Existing retained-wake
tests cover corrupt/stale RTC records; demand activation/retention suites cover
unchanged authorization, graph capacities and lifecycle behavior. CI runs these
suites normally and with ASan/UBSan. Host tests and target builds do not qualify
physical export, sleep current, wake reliability or SD-media durability.
