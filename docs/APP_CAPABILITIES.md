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
all are explicit owner-provisioned grants and still count toward the same 12-
grant bound. Non-KV capability uniqueness is unchanged. ID0 acquisition rejects
multiple matches; an app must name the intended namespace. This is provisioning consistency for
trusted native code, not a signature scheme or memory sandbox.

Limits are 16 app policies (`Runtime::MaxAppPolicies`), 10 distinct declared
capability types (`Runtime::MaxAppRequirements`), 12 independently authorized
grants per app (`Runtime::MaxAppPolicyGrants`), and 16 live app grants. These are
separate bounds: eleven requirement types, thirteen policy grants, or seventeen
policies reject before loading any ELF. No authorization is added implicitly.

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
