# Default-app provider promotion (Runtime0.1.50)

The existing contract below remains the default for `"demand"` and `"eager"`.
Runtime 0.1.63 adds the explicit [demand-retained policy](DEMAND_RETENTION.md),
under which the same authorized call pins active nodes and arms future retention
without loading absent providers. Its ABI and caller authority are unchanged.

This isolated integration preserves PR38 at
`0c6ceef38d1d972febd959e8f2da46c5a4a398ac` and PR39 at
`341e6e38ce00b7c57d5daaf5f8c829c3575db931` as merge ancestors.
Live branch/tag versions, open PRs and a0.1.50 issue/PR search were checked on
2026-10-07;0.1.49 was the highest live version and no0.1.50 claim was found.

## Integration contract

The configured default application's manifest must declare
`{"capability":"runtime.provider-promotion","api":1}` and its existing boot
policy must explicitly grant that capability with `instance_id:0`. The broker
returns `risc_provider_promotion_api_v1` from `RiscProviderPromotionV1.h` through
ordinary `risc_runtime_get_api(1)->acquire`. A child policy requesting this
capability fails admission, even if it lists the same requirement. No privilege
is inferred from package name, realtime access, wake class or selected providers.

Only the owner task inside the configured default's `app_main`, with the live
generation-bound grant, can call `promote(context)`. Init/fini, child invocation,
foreign task, copied stale context, queued handoff, native held/unsafe state,
retained metadata/app data/installed-files state, pending graph release, failed
module or graph reentry rejects before activation. Acquiring the table has the
same default/entry/safety checks. Only one promotion grant is live at a time.
During activation, recursive promotion returns BUSY; runtime acquire/release,
launch and graph polling cannot reenter the transition. Native code remains
trusted and cooperative, not memory-isolated.

The operation has no arguments identifying providers. It acquires one existing
boot-owned graph reference per selected node, using the admitted order and the
normal dependency-first graph loader. Full board/manifest/dependency/policy
validation still precedes any load. Existing complete cohort image inspection
and activation-time ELF/import admission are unchanged. Promotion never selects
another graph, rescans a store, imports raw hardware APIs or changes policy.

- OK: every selected provider now has a boot reference. This is one-way for the
  session; releasing the control grant does not demote providers.
- ALREADY_READY: eager boot or completed promotion. Still requires valid live
  authority and safe state; no provider is reacquired or restarted.
- FAILED: clean activation failure, including graph grant exhaustion. Successful
  prefix references remain boot-owned; retry resumes at the first missing node.
  Dependencies of the failed start follow existing verified cleanup behavior.
- RETAINED: activation or cleanup exposed uncertain native/graph custody. All
  Runtime app calls are immediately disabled, further promotion is denied, and
  app/graph mappings and dependencies remain pinned until restart. No fini,
  forced release, recovery call, or unload is attempted after this result.
- CONTEXT/BUSY: no activation side effect from this call.

Ordinary failed final quiescence retains existing graph/module/dependency
custody. Healthy promoted providers survive child handoffs and fresh default
invocations; boot-session exit uses existing reverse release/shutdown logic.
A clean partial promotion is deliberately not transactional rollback.

Demand-mode timer-only paths acquire nothing extra unless the application calls
promotion. A lazy display acquisition can start only its closure, then promotion
pins that same live mapping and activates remaining selected nodes. Scheduling,
display policy, wake decisions and product manifest edits belong to the central
X4 integrator. No X4 product files or previously delivered firmware are changed.

## Bounds and memory

Only `MaxAppRequirements` and `MaxAppPolicyGrants` change12→16. The live app
pool remains16. Graph modules/grants, native resources, namespace bounds and
serialized/shared hardware layouts are unchanged. The legacy graph still has32
grant slots; transient exhaustion during promotion fails cleanly and can be
retried after the caller releases redundant app provider grants. Paired targets
retain40 slots for24 boot pins plus16 app grants.

Xtensa8.4 object-symbol size measurements against the reconciled pre-change
commit `9d1d0f6`:

| Structure | Before | After | Delta |
|---|---:|---:|---:|
| Policy grant |24|24|0|
| Live app grant |44|44|0|
| Per-app policy |712|808|96|
| Legacy Runtime |208168|208184|16|
| Paired Runtime |236512|236528|16|

Policy metadata allocates only admitted count, so incremental storage is96 bytes
per policy:1824 bytes at the unchanged legacy19-policy maximum,2304 at the
paired24-policy maximum. Paired Runtime and policy metadata use the existing
PSRAM allocation policy without internal-RAM fallback. Graph safety checking
adds no stored state. Host paired Runtime size is250240 bytes.

## Software verification

The production Runtime/Graph/dlopen demand suite covers lazy closure then
promotion, eager/repeated calls, handoffs without restart, stale/reacquired and
foreign contexts, init/fini/child denial, reentry, held/native safety barriers,
clean failed activation with prefix preservation, failed-start cleanup custody,
pending failed release, uncertain native activation and final failed quiescence.
Normal and UBSan runs pass; CI runs the same suite with ASan/UBSan.
Local macOS ASan timed out before reporting results; no ASan pass is claimed.
Apple Bash3.2 empty-array and Mach-O dynamic-link differences in legacy runners
were adapted only in external harness copies, not production code.

Expanded KV/requirement tests admit13–16 distinct declarations and namespace
grants, reject17 independently, preserve duplicate/non-KV uniqueness and
undeclared/invalid namespace rejection, and exercise16 simultaneous live grants
with seventeenth rejection. Existing runtime/handoff, retained-app, realtime,
cohort/migration, paired metadata and sleep regressions remain required.

Host tests and target builds do not qualify physical sleep/wake, power,
display timing or hardware behavior. No device access, flashing, main merge or
release is part of this draft.

Single-job target builds (`pio run -e <environment> -j 1`, pinned stock flags):

| Environment | Static RAM bytes | Flash bytes |
|---|---:|---:|
| esp32s3 |263644|965029|
| esp32s3-16mb-appdata |60688|1249821|
| esp32s3-16mb-appdata-iq |60688|1250541|

Compared with PR39's recorded target builds, static RAM increases24 bytes on
legacy and0 on the paired targets (whose Runtime lives in PSRAM). The policy
allocation increase above is dynamic metadata and is not counted by the linker
static-RAM report. No hardware or delivered-image RAM measurement is implied.
