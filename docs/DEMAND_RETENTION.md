# Opt-in demand activation with session retention

Runtime 0.1.63 accepts the exact top-level boot-profile value
`"provider_activation": "demand-retained"`. Omission, `"eager"` and existing
`"demand"` behavior are unchanged. The new value is owner-provisioned policy;
neither wake class nor package identity selects it automatically.

Before promotion, the new policy has ordinary demand lifetime. An authorized
acquire activates only its exact provider and dependency closure. Releasing the
last consumer can stop/unmap them. A later acquire before promotion can therefore
start a fresh mapping. Selection, authorization and timer-only yields activate
nothing by themselves.

The configured default app uses its existing explicit
`runtime.provider-promotion@1`, instance-zero grant and unchanged
`risc_provider_promotion_api_v1::promote(context)` callback. With the new policy,
promotion pins only currently active selected nodes, then arms session retention.
It does not start or load absent nodes. In particular, a live display closure
acquired before promotion keeps the same mapping. Releasing the control grant
does not disarm retention; repeated valid calls return ALREADY_READY.

After arming, the first real authorized acquisition of an unpinned selected
provider obtains its session-owned graph reference before the app reference.
Its ordinary dependency-first activation runs once. That pin and its dependency
references survive app-grant release, default/child handoffs and fresh default
invocations. A dependency used only through a pinned parent is kept by the
existing graph dependency reference; a later direct acquisition may additionally
pin that node. Repeated acquisition of an already pinned node uses the original
app-grant path without new logs, graph safety scans, metadata reads or file checks.

The stage-log targets emit one successful arming statement:
`providers retention armed active=N deferred=M`. Existing named provider
load/start statements identify a later first use. Repeated promotion does not
repeat the arming statement. Counts describe active nodes when armed, including
dependencies, rather than all nodes that may be used later.

## Failure and authority

The existing default-only owner-task, app-main, live-generation, native custody,
metadata/app-data/installed-files retention, graph safety and queued-handoff
checks govern arming. Child/init/fini/foreign/stale callers remain denied.
First activation after arming is serialized against acquire/release, promotion,
launch, retain-invocation and graph polling reentry. Queued handoff denies a new
first activation before side effects. Existing acquisitions before arming retain
their original behavior, including the invocation barrier that suppresses a
queued child after failed-start cleanup retention.

A clean failed start follows the original graph cleanup and retry rules. Earlier
session pins remain owned. Failed cleanup or native uncertainty immediately
fences app API authority, preserves mappings and dependencies, and prevents fini,
unload or subsequent launch. No new recovery or forced unmap path is introduced.
If an already successful session pin consumes the last graph grant slot, app
acquisition fails cleanly while keeping that pin. Releasing another app grant
allows retry without reloading the provider. Promotion itself preserves any
successfully acquired prefix of active pins on a clean failure.

Boot/session shutdown visits the per-driver pin slots in reverse selected order,
skipping holes. Graph consumer counts still determine dependency-safe teardown.
Failed quiescence retains the same pending grant, module and dependencies as
before. A partial graph or an armed but entirely unused graph shuts down without
loading any absent provider.

## Admission, bounds and source boundaries

Complete board, typed configuration, selected manifests, hardware ownership,
dependency graph and application policy admission remains mandatory before any
provider load. `inspectImages` still enumerates every selected provider and app;
installation/update image and import checks are unchanged. Actual ELF mapping,
loaded-image bounds, relocation and provider ABI validation use the original
loader. No checksum, file-change/corruption probe, unused-image boot preflight or
touch/launch re-verification gate is added.

There is no shared ABI layout change, new grant pool or graph allocation.
Session references use the existing fixed boot-grant array, indexed by selected
driver so absent nodes can leave holes. Legacy capacities remain 17 providers,
32 graph grants and 16 app grants; paired capacities remain 24/40/16. Legacy
17-provider sessions can therefore hold 15 simultaneous provider-backed app
grants, exactly as the previous fully promoted graph. Exhaustion rejects and can
be retried after release. Policy limits stay 16 requirements and 16 grants.

This generic Runtime slice changes no product profile, manifest, source lock,
hardware policy or delivered image. Product adoption requires the new explicit
profile value and the existing authorized foreground promotion call. Timer-only
paths should continue to omit promotion when session retention is unnecessary.

## Reproducible software evidence

`bash test/run_demand_retention_test.sh` compiles real host shared modules and
executes production Runtime/Graph/Module code. Production named load statements
and the existing performance recorder measure loads, starts and graph acquire
calls. The same harness can measure the exact pre-change source with:

```sh
RUNTIME_SOURCE=/path/to/c546dae-checkout \
  MODES='baseline-eager baseline-demand' bash test/run_demand_retention_test.sh
SANITIZE=1 bash test/run_demand_retention_test.sh
```

The baseline is `c546dae32e2e75f7e7f4867dc788b6be53ded64c` (0.1.62).
For a three-node graph (`leaf` requires `root`; `unused` is independent), the
same leaf-use/default-to-child-to-default sequence measures:

| Policy | Total provider loads | Starts | Graph acquire calls |
|---|---:|---:|---:|
| Baseline eager | 3 | 3 | 7 |
| Baseline demand plus existing promotion | 3 | 3 | 7 |
| Demand-retained plus promotion | 2 | 2 | 6 |

Baseline demand and demand-retained both load the two-node leaf closure before
promotion. Existing promotion then loads `unused`; the new policy leaves it
untouched. All used mappings survive the three app invocations. If the child
later really acquires `unused`, the new policy reaches 3 total loads/starts and
that provider still starts only once across the next default invocation.

Timer-only yields and arming an unused graph both measure 0 loads, 0 starts and
0 graph acquires. Releasing the leaf closure before arming then acquiring it
afterward measures 4 loads/starts, proving that pre-promotion acquisitions are
not silently cached. The suite also verifies no additional metadata read/parse
events after entry, strict policy rejection, malformed unused metadata rejection,
complete image enumeration/rejection, reentry and owner/stale/child/init/fini
denial, clean retry with existing pins, failed cleanup/native retention, queued
child suppression and partial/failed shutdown.

The maximum-graph cases execute all 24 providers with 40 graph slots and all 17
legacy providers with 32 slots. They reject one extra selected node, exercise
full app/graph grant pools, and prove legacy slot exhaustion after a successful
new session pin can retry without a second start. The legacy host variant uses
the exact legacy branch of `RuntimeLimits.h` in a temporary include directory;
it does not change production limits. CI runs the suite normally and with
ASan/UBSan. Existing demand/eager/promotion, lifecycle, cohort, provider graph,
sleep, metadata and loader regressions remain applicable.

Version 0.1.63 was reserved on 2026-10-08 after checking all 56 live refs/tags,
their firmware versions, 16 open PRs and an exact version-claim search, plus
coordinating with X4 and Watch owners. Source remains local for central
integration; no publication ancestry authorization is inferred. Host/target
builds do not qualify physical timing, power, wake or hardware behavior.
