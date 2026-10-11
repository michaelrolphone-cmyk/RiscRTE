# Provider-bound key-value storage v1

Firmware 0.1.7 adds the canonical driver SDK contract
`sdk/driver/RiscBoundKeyValueV1.h`, capability `storage.key-value.bound@1`.
It reuses the existing optional NVS backend without changing its implementation,
partition layout, initialization, no-erase behavior, or app `storage.key-value@1`.
It adds no provider ELF ABI fields or exports, scheduler, output policy or UI.

## Exact selection authority

A selected boot driver may have one optional `key_value` list:

```json
{
  "manifest": "example/driver.json",
  "key_value": [
    {"key": "configuration", "namespace": 3, "access": "read"},
    {"key": "state", "namespace": 4, "access": "read-write"}
  ]
}
```

Its manifest must declare exactly one requirement
`{"capability":"storage.key-value.bound","api":1}`. The list applies only to
that exact selected manifest/path/instance. An ordinary software driver remains
a singleton without `instance_id`; a hardware driver follows the existing exact
hardware selection rules. No inferred package privilege or fake hardware ID is
needed. Hardware bindings must not select this reserved storage capability.

The list has 1–9 unique entries (the ninth was added in firmware 0.1.26). Each entry has exactly `key`, `namespace` and
`access`; namespaces are integers in 1..2147483647, and access is exactly `read`
or `read-write`. Keys are 1–15 ASCII bytes in `[a-z0-9_.-]`. Missing/empty/null or
non-array maps, unsupported API, duplicate requirements/keys, invalid bounds,
unknown fields/access modes and unavailable backend callbacks fail before any
ELF opens. The matching requirement and nonempty map must occur together.
An ordinary provider cannot provide the reserved capability; an app cannot
declare or receive it. Missing policy never falls back to a same-name ELF.

The driver gets one dependency table with get and put callbacks. The table has
an opaque context; calls supply only a key, never a namespace. A key is forwarded
unchanged into its authorized namespace. Unlisted keys and writes to read-only
keys return CONTEXT without any backend I/O. Both callbacks remain present even
for an all-read-only table; denial is enforced by the broker. There is no alias,
enumeration, open/close, delete, filesystem, transaction or unrestricted namespace
API. Runtime stores at most 16 maps of ten keys each; calls allocate nothing.

The example namespaces and keys above are deployment policy, not firmware
defaults. Existing app KV grants continue to cover their entire namespace. A
provider's per-key map does not narrow an app's grant to the same namespace. To
keep provider-owned records separate from writer apps, place them in a namespace
that no app receives. Encoding, revisions, validation and safe defaults remain
the responsibility of the external applications/providers.

## Authority lifetime

The internal `ModuleLeaseV2 {context, begin, revoke}` value is optional, host-only
and default-empty. All three fields must be present together or all absent.
Graph owns a by-value snapshot and installs it only on an absent unmapped module.
This is private host lifetime plumbing, not an ELF-visible setter or ABI suffix.

1. Prepare validates/copies maps and pins dependency tables with no live context
   and no backend I/O. Dependency, ELF-open, descriptor identity/ABI and stream
   admission failures create no storage authority.
2. After descriptor validation and successful stream binding, begin runs
   immediately before provider start. It mints a fresh process-wide opaque
   integer token, never dereferenced or reused. Exhaustion fails closed.
3. Storage works during start and active lifetime on the Runtime owner task,
   including before a foreground app exists and across default/child handoffs.
   Releasing a foreground consumer leaves a boot-held provider live.
4. Failed begin skips start. Failed begin/start revokes before diagnostics,
   quiesce or stop; diagnostics may reenter provider code but cannot use storage.
   Revoke is idempotent and performs no I/O.
5. Normal unload first refuses while consumers remain, then revokes before
   quiesce/stop. Failed quiescence or close retains code, table/map storage and
   dependencies, with authority dead throughout any cleanup retry.
6. Only successful unload followed by fresh admitted activation can mint a new
   token. A copied callback/context fails after revoke, Runtime replacement or
   generation exhaustion. A table pointer expires at revocation and must not be
   dereferenced later, even though physical storage remains pinned for cleanup.
7. Every call checks current Runtime, owner task, live exact context and native
   retention barrier. A native fault inside an app revokes all provider storage
   immediately on its next call, before the app returns to the outer barrier.
   Healthy I2S activity (since0.1.12) and station radio activity do not alone
   revoke storage; failed/closing I2S, native transfers and retained state do.
   Every live I2S stream still blocks app exit and sleep independently.
   Retention revocation invokes no provider cleanup. All run exits revoke before
   clearing current Runtime. Destruction revokes before graph destruction.

Runtime binding storage is declared before Graph, so Graph is destroyed first.
Existing graph fail-stop destruction prevents freeing dependency/table storage
while mapped providers or live grants remain. The production Runtime has static
lifetime and deliberately retains an uncertain physical graph until restart.
These checks enforce lifecycle for trusted native code, not memory isolation
against malicious code that can inspect another provider's token or memory.

Cleanup must not require durable storage writes: quiesce and stop have already
lost this authority. Physical quiescence/retention is still enforced separately;
revoking storage never establishes physical safety.

## Values and failure semantics

Values remain opaque 1–64-byte blobs. Numeric statuses match app KV without
sharing its header or changing its contract: OK 0, NOT_FOUND -1, BUFFER_SMALL -2,
INVALID -3, CONTEXT -4, IO -5.

Get requires `out_size`, resets it to zero on error except BUFFER_SMALL, and
copies bounded scratch data only after a complete valid backend success. A
NULL buffer with zero capacity probes the size; a present value returns
BUFFER_SMALL with the required size. NULL with nonzero capacity is INVALID.
No error exposes partial data. Valid but unauthorized keys/operations return
CONTEXT, not NOT_FOUND. Backend missing is NOT_FOUND; other backend errors and
zero/oversized returned blobs are IO. The SDK can hide corrupt/type-mismatched
lookups behind missing; consumers must apply their own safe default.

Put OK means the existing backend's commit plus exact readback succeeded. IO
has uncertain durability: the new value may or may not have persisted; rollback
is never promised. There is no retry, erase, format or hidden recovery policy.
See [the existing NVS contract](KEY_VALUE.md) for pinned SDK behavior. Calls are
synchronous and have no hard latency guarantee; adding them to a provider poll
does not prove compliance with its cooperative time budget.

## Verification scope

`test/run_bound_key_value_test.sh` exercises real Runtime with dynamic providers
and apps, schema/rights checks and observed backend calls. The separate
`test/run_provider_module_lease_v2_test.sh` covers real Module/Graph hook ordering,
failed activation, retained cleanup, retries and fresh activation. Run both with
`SANITIZE=1` for ASan/UBSan. Existing app KV/NVS-no-erase, board, graph, Watch,
retained-app, Light/Deep/timed-sleep, SDK shim and actual target ELF regressions
remain part of integration CI.
`scripts/build_apps.py` also compiles a separate test-only bound-storage provider
into a real Xtensa ET_DYN image, checks its export/import surface, and feeds the
production ELF validator. It is not staged into the boot store.

Host fixtures and target CI builds do not qualify physical persistence,
power-loss behavior, latency, wake reliability, output safety or current draw.
No complete application service or product feature ships in this prerequisite.

The ten-key admission bound preserves the Points catalog migration metadata alongside its existing settings, occurrence, output-policy and timezone keys. Each name and access mode is still explicit; an eleventh entry is rejected before provider construction or backend I/O. The added fixed map row costs 384 bytes across all sixteen provider slots on the 32-bit target.
