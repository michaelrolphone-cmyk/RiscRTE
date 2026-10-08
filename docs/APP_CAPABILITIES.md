# App capability extension v1

`sdk/app/RiscRuntimeV1.h` retains its original ABI prefix and appends acquire and
release. Require api_version1, struct_size>=RISC_RUNTIME_CAPABILITIES_V1_SIZE and
both callbacks. Initialize `risc_runtime_capability_v1.struct_size`, then acquire
by capability/version and optional exact instance ID. Zero means the uniquely
authorized selected instance; no registry-order fallback. Before casting api,
check the capability's own version and full table size. Release capability-owned
frames/sessions first, then release the grant. App resources and callbacks must
not outlive app_main/fini. Remaining grants are revoked before memory/image
teardown; failed revocation retains the invocation and blocks handoff.

The owner-provisioned immutable boot store declares policy, not the app itself:

```json
{
  "board": "board.json",
  "default_app": "default.elf",
  "drivers": [],
  "app_capabilities": [{
    "manifest": "default.json",
    "grants": [
      {"capability": "display.output", "api": 1, "instance_id": 5},
      {"capability": "rtc.clock", "api": 2, "instance_id": 8}
    ]
  }]
}
```

Populate drivers with the explicit external driver closure. A referenced app
manifest has typeapplication, id, version, architecturextensa-esp32s3,
file_namedefault.elf, entryapp_main, and requires entries `{capability,api}`.
The ELF is the manifest's safe basename beside that manifest. The loader binds
this exact path to manifest identity/version and the intersection of declared
requirements with authorized grants before any ELF runs. Missing, extra,
duplicate or ambiguous grants fail boot. The sole additive exception is multiple
distinct, positive storage.key-value@1 namespaces for one manifest requirement;
all are explicit owner-provisioned grants and count toward the compiled policy
row bound (16 by default). Non-KV capability uniqueness is unchanged. ID0 acquisition rejects
multiple matches; an app must name the intended namespace. This is provisioning consistency for
trusted native code, not a signature scheme or memory sandbox.

Limits are target-dependent app policies (`Runtime::MaxAppPolicies`, unchanged),
16 distinct declared capability types (`Runtime::MaxAppRequirements`),
16 independently authorized policy rows per app (`Runtime::MaxAppPolicyGrants`)
by default, and 16 live app grants. Requirements reject at17; policy rows reject
at17 by default, or at18 under the explicit 17-row native build option;
policy count rejects above its existing target bound. No authorization is added
implicitly. Runtime0.1.50 raises only the declaration/policy limits from12 to16.
See [default-only provider promotion](PROVIDER_PROMOTION.md).

Runtime0.1.68 adds only `-DRISC_APP_POLICY_ROWS=17`; absence or an explicit16
retains the prior metadata layout. Other values fail compilation. The additional
immutable row permits, for example, sixteen unique requirements with two
independently authorized KV namespaces for one requirement. It adds no namespace
or permission by itself. Live-slot exhaustion/release/generation ownership and
the sixteen-requirement bound remain unchanged. See [build selection, marker,
memory cost and verification boundaries](APP_POLICY_ROWS.md).

Internal policy records reference only already-retained immutable capability
names in the fixed driver/platform table, or the canonical KV literal. They never
borrow parser memory. Runtime is noncopyable/nonmovable; preparation and platform
registration cannot replace metadata after admission.

Firmware 0.1.27 expands policy slots from ten to twelve while compacting private
indices to signed8-bit values. Fixed16-driver/32-platform capacities are asserted
against that index width; -1 remains the absent sentinel. Board binding IDs use
32-bit private storage only after the unchanged1..INT32_MAX parse check. The shared
64-bit hardware IDs and all serialized layouts remain unchanged.

Pinned Xtensa GCC8.4 measures Runtime at214,272 bytes, down4,608 from its immediate
0.1.26 parent (218,880), despite the two extra grants. CpuPort is4,648 bytes versus
4,384 (+264), a net fixed-data saving of4,344 bytes. Generic USB and paired retain
the existing retained-PSRAM allocator; baseline/CAM/X4 retain internal metadata.
No target gains a new PSRAM requirement or loses capacity or validation.
These records are never serialized or exposed through an SDK. Default/child
reload, parsed-memory churn, rejected reprepare, failed prepare and stale-handle
regressions run with the same exact authority checks.
No grant is added implicitly; each new namespace must still be declared in
the boot policy. All grant handles have nonreused generations; stale handles, wrong API/instance,
short output structs and calls outside the owner app are rejected. Child paths
without their own policy get no capability grants; policy is not inherited.
Health, diagnostic, cooperative yield and launch remain available through the
original ABI. Apps using only that prefix need no policy/manifest change.

Ordinary selected capabilities are opaque to the runtime. The example numbers
are deployment selections, not runtime code. `display.output@1` uses the complete
canonical display table. `rtc.clock@2` may use a driver's existing table; a clock
app can call only read and show TIME UNSET on invalid time, without seeding it.
Raw platform GPIO/SPI/hardware records are not exposed to apps. Optional native app services are explicitly granted global platform.clock and
namespace-bound storage.key-value@1 (see KEY_VALUE.md); ordinary
clock apps can use health.uptime_ms and yield_ms instead. yield_ms continues the
inherited bounded provider poll dispatcher so queued display work progresses.


Native retention (firmware0.1.5) is checked by an optional compiled-in port
callback before fini/unload and again after fini. A failed native barrier
logically revokes app grants and disables owner APIs while retaining the image,
allocations and provider references, rather than invoking cleanup or launching
another app. This is separate from ordinary capability release and graph
quiescence; see DEEP_SLEEP.md.

Policy metadata is allocated for the actual validated count, bounded at 18, with no allocation when the policy list is absent/empty. Paired and explicit metadata-PSRAM targets preserve their PSRAM-only allocation policy; allocation failure rejects prepare. Policy storage outlives the provider graph and all retained grant names.

Explicit `file.open@1`, instance0, supplies bounded file associations and an
unload-before-open handoff using the canonical Reader table. Handlers come only
from selected boot-policy manifests; data paths never become ELF launch names.
See [file dispatch, result and authority semantics](FILE_OPEN.md).

Runtime0.1.51 adds an optional owner-only terminal `retain_invocation` suffix
and automatic pre-fini/pre-unload fencing for failed graph state. It conveys no
new provider authority and performs no cleanup. See [invocation retention](INVOCATION_RETENTION.md)
for exact signal, idempotence, compatibility and cleanup-custody semantics.
