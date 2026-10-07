# Opt-in provider demand activation

Runtime 0.1.48 adds the optional top-level boot-profile field
`"provider_activation": "demand"`. Omission and explicit `"eager"` preserve the
existing boot-owned acquisition of every selected provider. Only those two exact
strings are accepted; null, booleans, numbers, objects and other strings reject.
This is owner-provisioned selection policy, never inferred from wake class.

Demand mode skips only the eager boot-owned acquisitions. The existing app
`acquire` activates its exact authorized provider and transitive dependency closure,
dependencies first. Merely selecting or authorizing a provider does not start it.
The policy grants no new capability, instance, import or package authority.

## Validation boundaries

`Runtime::prepare` still validates the complete board, typed configurations,
selected manifests, hardware compatibility/ownership, provider storage policy,
missing/ambiguous dependencies, cycles and app grants, then registers every node.
Malformed unused selected metadata still rejects before any provider starts.
`inspectImages` still enumerates all selected provider and app images. Native
provisioning and cohort admission retain their complete image/role/import checks.
Actual provider activation retains the ordinary ELF loader and ABI validation.
`prepare` itself is metadata admission, not an ELF-byte inspector; this feature
does not add an every-boot unused-image preflight or bypass staged admission.

## Lifetime and integration

In demand mode there is no boot reference keeping an otherwise unused provider
active across applications. Existing graph consumer counts govern lifetime:
last successful release quiesces/stops/unmaps a provider, then releases its
dependencies. A later acquire loads fresh provider state. Multiple live grants
share the active instance. App fini runs before automatic grant revocation;
ordinary child handoff/failure still returns to a fresh default invocation.
Providers that need boot-session service can use the eager profile or the
explicit default-only [promotion capability](PROVIDER_PROMOTION.md) in0.1.50.

Failed starts and failed quiescence retain their existing dependency custody and
retry semantics. Failed app-grant revocation retains the invocation; native
retention fences still pin its code and graph before fini or release. No forced
unmapping, wake shortcut, or retry bypass is introduced. Product profiles must
opt in explicitly and assess their provider lifetime requirements separately.
No product/X4 profile is changed by this generic contract.

## Verification

`bash test/run_demand_activation_test.sh` loads real host modules through the
production Runtime/Graph. It checks unused-zero-start, eager/omitted equivalence,
dependency-first activation, shared grants, dependency-safe stop, clean failed-start
retry, failed-release retry, child denial/handoff/missing-child fallback, failed
cleanup custody, native retention before fini, invalid policy/unused metadata
rejection, and unchanged complete image enumeration/inspection rejection.
CI also runs the suite with ASan/UBSan. Host tests and target builds do not qualify
physical sleep, power consumption or hardware behavior.

Version reservation: 0.1.48 was checked against live branches, tags and open PRs
and reported to the coordinating task before publication. Parallel retained
realtime work is reserved separately at 0.1.49 from the same base
`3c39aa7ac50ecd7f6da0296a62a2d7af066f82d1`.
