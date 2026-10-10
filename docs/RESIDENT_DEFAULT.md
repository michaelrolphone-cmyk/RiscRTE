# Explicit resident default lifecycle

Runtime 0.1.82 adds a headless, opt-in host/foreground contract. It does not
contain Quick Actions, alarms, a launcher, a display snapshot, or a crash screen.
Those belong once in the resident default ELF and its product-owned adapters.
An unconverted application is not promised global controls.

## Selection and ABI

An owner-prepared boot configuration enables the role explicitly:

```json
"resident_shell": {
  "api": 1,
  "host": "default.elf",
  "foreground": ["browser.elf", "viewer.elf", "usb_sd_transfer.elf"]
}
```

The host must be exactly the configured default, with an admitted app policy.
Each foreground path must uniquely identify another admitted app policy. The
host cannot occur in the foreground list. Unknown keys, explicit null, wrong
versions, duplicates and unadmitted paths fail before provider activation.
The selected host and foreground ELFs must export the literal `risc_resident_app_descriptor_v1`
data object described by `sdk/app/RiscResidentShellV1.h`. Version, exact size,
role and reserved bits are checked before application init. The descriptor
alone grants no authority; an ordinary legacy profile ignores it.

The append-only `RiscRuntimeV1.resident_shell` getter copies an invocation-tagged
client only during admitted app entry. Old Runtime prefixes and the canonical
file.open header are unchanged. There are still 16 manifest requirements and
16 live capability slots per invocation; the shared graph capacity is unchanged.
The resident host is the only permitted owner of retained-wake authority. The
one native durable wake store cannot be partitioned merely by copying its API.
Foreground policies requesting that capability are rejected at admission.

## Serialized operation

The host registers one callback table, then calls `run_foreground`. Its native
stack, static state, mapping and allocations remain live while the child runs.
At a foreground `checkpoint`, Runtime changes invocation context and invokes
the dedicated host callback. On a clean return it restores the exact child
context and stack. Nesting is bounded to host -> child -> host callback; neither
the callback nor the child may recursively run another foreground invocation.
Dispatch is never inserted into yield, provider calls, init, fini or arbitrary
display waits. An app that does not cooperate cannot be safely preempted.

The thin foreground adapter must settle outstanding presentation, release
writable surfaces and borrowed buffers, and prepare app-owned resources before
calling a checkpoint. Native graph, stream, storage and exit barriers are then
checked. Opaque provider leases cannot be inferred from a callback name.
The host must finish overlay/restoration presentation, consume input and require
neutral input before returning rendering authority. Product display/input
proxies must authenticate the focused invocation. Runtime grants do not turn
two independent physical display/input consumers into safe concurrent users.

`OK`, clean `BUSY`, `EXIT`, `DENIED`, `INVALID`, `INCOMPATIBLE`, `FAILED` and
terminal `RETAINED` are distinct. `BUSY` commits no focus/configuration change.
`EXIT` is cooperative: the foreground returns promptly, then normal checked
cleanup runs. The host can request exit only during its dispatch callback.
Unknown checkpoint reasons remain representable for future adapters; a host
that cannot handle one may return clean BUSY without modifying state.

Hosted `request_launch` is restricted to the admitted foreground set and runs
the next child only after teardown. `request_default` ends the child chain and
returns to the resident host without reloading it. The host uses
`run_foreground`, not these destructive handoff APIs. Ordinary foreground
file.open caller/receiver/result handling remains fresh per child invocation;
explicit Home suppresses caller return only after successful cleanup. This
version does not make the resident host a file.open caller.

## Explicit legacy compatibility

Runtime 0.1.86 accepts an optional `resident_shell.legacy` array, for example
`"legacy": ["legacy_viewer.elf"]`. Each entry is an exact, normalized path to an
admitted application policy. It must not be the host, a foreground entry, or
another legacy entry. Unknown keys, null, non-array values, invalid paths and
unadmitted paths fail before provider activation. The array may be empty;
omission preserves the previous resident-only selection. Ordinary profiles
without `resident_shell` keep their existing behavior.

A listed legacy ELF runs unchanged under its exact existing capability grants.
It does not need a resident descriptor and cannot acquire a resident client,
register callbacks, run a hosted child or invoke a resident checkpoint. The host
has already completed fini, grant and stream cleanup, allocation cleanup and
unload before legacy init begins. No resident callback runs during legacy init,
entry, yield, fini or cleanup. Existing provider ownership and retained failure
barriers still apply; legacy selection adds no hardware or resident authority.

The existing client layout and Runtime prefix remain unchanged. Two new result
values and the explicitly documented null-path resume operation are used:

- `run_foreground(token, "legacy_viewer.elf", &result)` copies a handoff request
  and returns `RISC_RESIDENT_HANDOFF` (3). A hosted child's `request_launch` or
  file-open dispatch to an admitted legacy path produces the same host result,
  but only after the child stack has returned and all child cleanup succeeded.
- On `HANDOFF`, the host must return from `app_main` promptly. It may perform its
  normal checked unwind/fini, but must not restore focus or render another UI.
  Runtime never tears down the host recursively from a live foreground stack.
  Host cleanup failure discards the pending launch, pins uncertain custody and
  enters retained state. A retained result is never represented as `HANDOFF`.
- A legacy ELF may return, call `request_default`, or use `request_launch` with
  an admitted legacy/foreground path or the configured default. Legacy return
  and explicit Home start a fresh default host. A resident target is copied as
  a continuation and runs only after a fresh host has initialized and registered.
- Each fresh host calls `run_foreground(token, NULL, &result)` after
  `register_shell`, before its ordinary first screen. `RISC_RESIDENT_NO_PENDING`
  (4) means there was no saved continuation and `result.invocation` is zero.
  Otherwise the method runs the saved foreground chain and returns its ordinary
  status, including `HANDOFF` if that chain selects a legacy app. Explicit Home
  leaves no saved continuation. A normal non-null launch while a continuation
  is waiting returns `BUSY`; returning from the host without consuming it fails
  explicitly instead of silently discarding it. Allocation failure before the
  continuation starts leaves it pending for an explicit host retry.

Cross-mode file-open preserves its original copied source path, caller/receiver
policy indices, cookie and result. No callbacks, grant tables, allocation
pointers or ELF pointers cross a handoff. A legacy receiver's clean return or
load/init failure starts a fresh host and fresh resident caller, delivering the
result once. A resident receiver may similarly return to its fresh legacy
caller after checked host teardown. Explicit Home suppresses caller return only
after the receiver succeeds and cleanup completes; retention suppresses every
handoff. Legacy failure records remain readable by the next host through
`last_failure`; no callback into the former host is attempted.

Old host/child tokens and copied native or stream contexts remain invalid during
the legacy invocation and after the new host starts. Cooperative resident
controls are unavailable while a legacy app runs. This route qualifies generic
Runtime lifecycle compatibility, not the UI or behavior of any product app.

## POLL and settled POLICY checkpoints

Runtime 0.1.89 preserves the request/reply/client layouts and existing zero-flag
checkpoint behavior. `POLL` alone accepts three explicitly defined request bits:
`RISC_RESIDENT_POLL_ACTIVITY` (1), `RISC_RESIDENT_POLL_INHIBIT_IDLE` (2), and
`RISC_RESIDENT_POLL_INHIBIT_POLICY` (4). Unknown bits and nonzero flags on any
other reason are invalid before host dispatch. Activity describes latched
physical input; the two inhibition bits distinguish inactivity policy from any
policy work that could disrupt capture or other app-owned operations.

A lightweight POLL keeps child touch, focus and subscriptions in place. It does
not run application policy, close/reopen input, draw an overlay, or perform
storage/radio/sleep work. The host may return `RISC_RESIDENT_REPLY_POLICY_REQUEST`
(4) with `OK` to request a later settled `RISC_RESIDENT_CHECKPOINT_POLICY` (7).
That reply is invalid on another reason, a non-OK result, a simultaneous exit,
or a POLL containing `INHIBIT_POLICY`. `INHIBIT_IDLE` alone does not forbid a
policy request, since low-battery work may be due even while idle sleep is not.

Runtime records the request against that foreground invocation. A POLICY call
without a pending request is invalid before dispatch. BUSY preserves pending
state. Successful non-inhibited POLLs without a new request also preserve it;
successful POLL with `INHIBIT_POLICY` cancels it. Successful POLICY, cooperative
EXIT, child return and terminal retention clear it. Nothing transfers that
permission to a replacement invocation or a legacy handoff. Malformed host
replies keep the existing terminal-retention behavior.

Before POLICY, the application rechecks that capture and app-owned work are
settled, writable/borrowed buffers and presentation tokens are released, and
input is neither held nor queued. It pauses background/service work and closes
child touch/focus before checkpointing with zero flags. If any condition is
unsafe it defers the call, keeping pending state. The host may then perform its
product-owned low-battery/settings work or typed reversible Light sleep. Clean
OK/BUSY returns restore child focus/touch and neutralize inherited input;
terminal retention permits no further provider calls. BUSY must commit no host
policy/focus/configuration change. Product code decides whether redraw or
configuration refresh is needed after successful POLICY.

The client clears latched ACTIVITY only after successful POLL handling, and
preserves both activity and policy state on BUSY. Requests are copied before
host dispatch; replies are copied only after clean child-context restoration.
These flags add no resource authority and do not weaken the existing native,
provider, storage or stream barriers. In particular, a raw capture that makes
`appExitSafe` false must defer POLL as well as POLICY until clean release.
`INHIBIT_POLICY` cannot bypass that boundary. Deep sleep and restart still
require clean child exit before the host's terminal operation.

## Ownership and failure

Host and foreground have separate grant banks, stream brokers, native tables
and generation tokens, installed-file handles, app-data namespace bindings and
pending file/navigation state. Grant generations do not reset on a switch.
Copied callbacks/stream clients reject while their invocation is suspended and
after release or replacement. Providers and immutable policy metadata remain
boot-session owned. Native code is trusted, not a memory-security sandbox.

The native allocator keeps at most two ledgers, each with the existing 4096
allocation bound. Child cleanup reclaims only its ledger. A cross-context free
finds the real owning ledger, preventing a later double free. Reallocation
requires current ownership. Foreign-task allocation and context selection are
refused. Context creation and selection failures never discard host memory.
Each target ledger consumes 49,152 bytes of PSRAM metadata; a second ledger and
foreground record exist only when used. Installed-file configuration is copied
for the foreground so its handles cannot close the host's handles.

Uncertain cleanup in resident mode fences both invocations immediately, revokes
ordinary grant/stream authority, and pins both mappings, ledgers and provider
dependencies. No fini, free, unload, implicit retry or ordinary polling follows.
The first retained diagnostic is preserved. A retained stack may return through
its caller, but that does not restore authority. Legacy profiles retain their
existing checked release/retry semantics and destructive navigation lifecycle.

Clean child load/init/ABI/allocation failures return a copied result and may
notify the host's failure callback after cleanup. Failure inside that callback
is itself fenced and reported as RETAINED. Terminal retention never invokes a
new UI callback. A cached host client can read the copied terminal record while
still on the current host stack; it cannot render using revoked capabilities.

The native port also exposes read-only evidence of a previous panic, watchdog
or brownout reset. That record contains the native reset reason, not an invented
application identity or stack trace. CPU faults are not caught or resumed by
this API. A product can show its restart screen when the next host starts.

Deep sleep and native restart are refused while the foreground chain is live,
including host dispatch. The child must return and clean up first. Light sleep
keeps its existing native resource checks. No arbitrary foreground model is
promised to survive RAM loss. The host remains responsible for its exact-identity
durable clock checkpoint and ordinary sleep preparation.

## Qualification and integration limits

The focused suites run actual host/foreground ELFs and production Runtime,
provider graph, stream brokers, native allocation ledgers and context switches.
They cover repeated child launches and callbacks, stale/wrong-owner calls,
file.open and Home, simultaneous streams, native storage isolation, allocation
pressure, and failure/retention boundaries. Legacy Runtime, file-open, demand
activation/retention, sleep, update admission and allocation-pressure suites
remain required. CI executes the focused suites normally and under ASan/UBSan.

`test/run_resident_legacy_test.sh` adds real host/foreground/legacy mappings,
constructor/destructor ordering and fresh globals through repeated mixed
chains. It checks exact admission, both directions of file-open continuation,
Home and launch requests, clean init/load failures, stale callbacks and tokens,
failure records, unconsumed continuations, and host/child/legacy retention at
native, grant, stream and allocation cleanup boundaries. Set
`RESIDENT_NATIVE_MEMORY=1` to exercise production allocation ledgers and
`SANITIZE=1` for ASan/UBSan. The fixtures are generic and do not qualify or modify
any product application.

`test/run_resident_policy_test.sh` exercises production Runtime, provider graph,
streams and native deep-entry gates through real synthetic ELF mappings. It
covers every POLL bit combination, invalid reasons/flags, copied request/reply
buffers, invocation-bound policy requests, BUSY preservation and cancellation,
teardown isolation, malformed host replies and retained custody. Existing
zero-flag checkpoint and legacy handoff regressions remain required.

A successful native target link does not qualify product memory/stack peaks,
panel completion/restoration, input neutrality, alarm arbitration, USB throughput
or physical sleep. Product integration must measure the selected host plus its
largest child and snapshot against the 16 KiB owner-task stack and available
PSRAM. No product cohort, provisioning payload, release or device is changed by
enabling this source capability alone.
