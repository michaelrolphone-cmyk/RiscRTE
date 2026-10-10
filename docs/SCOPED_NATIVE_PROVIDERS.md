# Explicit per-provider native import admission (0.1.92)

This is a generic, default-off firmware admission path. Ordinary builds return
an empty `selectedNativeProviderPoliciesV1()` set. No current product, USB
controller, board mapping or physical device is selected by this change.

## Authority and exact selection

A trusted compiled-in caller supplies `Port::nativeProviders`, or a build-owned
`RISC_NATIVE_PROVIDER_POLICY_HEADER` defines the immutable selected policy set.
The native main entry and provisioning candidate use that same compiled set;
staged cohort admission inherits the running Runtime's owned selection. At
initial preparation Runtime copies the complete set, including currently
unselected entries; later changes to the caller's arrays cannot change staged
authority. The policy
header is a build input, never a path taken from boot JSON or a package.

Each entry pins the relative executable path, provider ID/version, capability
and API, OS/CPU ABI 1, diagnostic ABI (0 or 1), exact image length/SHA-256, complete sorted unique import
set and exact dependency capability/API declarations. The entire policy set is
bounded to 16 providers, with 128 imports and 16 requirements per provider.
An entry matching either the selected ID or path must match all selection
fields; mismatches reject instead of falling back to ordinary admission.

Manifest fields, `provider-abi.v1`, `privileged-imports.v1`, a capability name or
a checksum alone cannot populate this native policy. Driver manifests and boot
JSON keep their existing strict schemas. The checksum pins content chosen by
the trusted policy; it is not a signature or independent source of authority.
The native executor and policy APIs are absent from ELF exports and consumer
capabilities. Native code remains trusted and is not memory-isolated.

## Owned bytes and lifecycle

Runtime validates the full board/configuration/dependency graph before reading
or registering the selected native images. It reads the exact expected file
length with owner/safety checks and a 30-second deadline, and treats a failed
close as retained metadata custody. If native owner/storage safety is lost,
further reads and close are fenced. The exact FILE and potentially borrowed
image buffer remain reachable in Runtime custody. NativeBankStore retains the
entire candidate Runtime (and provisioning CPU) and mounted session on this
path, including an uncertain failed close. Recovery is terminal-only; restoring
an owner/safety flag cannot authorize cleanup, reuse or selection. Destroying
such a Runtime is a caller-contract violation, guarded like retained graph
destruction.
Graph registration copies image, metadata
and the selected policy. It hashes the graph's own copy and checks ELF structure,
the actual dynamic provider entry, and exact imports across both symbol tables.
A provider image must expose one global text `t5_driver_get` function and no
application entry or lifecycle exports. Registration executes no provider code.

`ModuleV2::loadVerifiedBytes` requires the graph-bound image address, length and
owned policy. Supplying the public byte/hash/import arguments without that
binding cannot grant loading. Each mapping gets a fresh temporary image copy;
its full digest and exact selected policy are checked again before relocation.
There is no cached-hash shortcut for a changed owned snapshot.

The existing dependency pins, module leases, stream grants/queues, failed-start
quiescence, retained cleanup and checked unload paths are reused unchanged.
A native provider must supply quiescence. Failed start cannot release mapped
code or dependencies before cleanup succeeds, and a native retained lease or
stream custody barrier continues to block unsafe callbacks/unmapping.

## Exact relocation and staging

The new selected relocation entry reuses the existing structural/import checks
and one-shot task-owned module relocation scope. Its resolver exposes only the
selected imports while that exact relocation is active. Another task remains
on ordinary lookup; nested, unbound or repeated relocation cannot consume the
scope. Public lookup tables are never enlarged or replaced.

Supported names are the actual strongly linked OS/CPU ABI inventory, plus the
intersection of the inherited ABI-1 libc compatibility inventory and the
selected firmware's real public libc table. Customer exports and other loaded
modules cannot supply authority. Some newer ordinary libc names are outside
that inherited compatibility inventory and remain refused by this native path.
Ordinary app/provider lookup is unchanged.

Production `NativeBankStore` asks the prepared candidate Runtime for its exact
owned policy for a driver path. It applies the same byte/digest/entry/import
checks during provisioning and full-cohort staged admission. Applications and
unselected extra ELFs always retain ordinary import admission, even if their
bytes or declared capability resemble an approved provider. A change to the
staged file after Runtime preparation fails the repeated exact image check.
The selected provider's second read in the bank adapter also fences EOF/close
and retains its exact Runtime, FILE and image buffer on owner/safety loss or
uncertain close. Ordinary app/unselected read paths remain unchanged.

## Separately selected bounded diagnostic ABI

Diagnostic ABI 0 (the default) continues to refuse `printf`, `puts` and
`putchar`. Only a trusted policy explicitly choosing `diagnosticAbi=1`, listing
its exact diagnostic imports, and linking the enabled bounded sink can select
these native substitutions. Legacy fixed-inventory scopes cannot bind them.
Ordinary public tables remain unchanged. See [the complete format, ownership,
return and loss contract](PROVIDER_DIAGNOSTICS.md).

The opt-in Reader `usb-controller-esp32s3@0.1.24` image has 47 imports: six from
the ordinary libc compatibility intersection, 38 from the private OS/CPU
inventory, and three bounded diagnostic substitutions when ABI 1 is selected.
The pinned controller can pass the exact import check in that explicit test
selection. Default builds still select no controller, and imports alone do not
qualify physical admission.

Board VBUS/role qualification, exact product selection/grants/bindings, USB
protocol behavior, physical host activation and hardware qualification remain
separate work. No pin assignments, native provider ABI layout, app permission,
SDK header or frozen .44 product image changes here. Publication is stopped
because this Runtime branch retains the separately stopped .82 ancestor.

## Verification

Focused selected-policy tests use production graph/module/relocation and bank
admission code with simulated hardware and mapped-entry boundaries. They must
cover default-off/ordinary rejection, exact selected success, malformed/forged
or undeclared imports, changed bytes and caller metadata, wrong-task/nested
lookup, failed starts, stream and native retention, and checked cleanup retry.
Normal and sanitizer runners and target compile/link results are recorded in
the local qualification receipt. A host harness does not execute Xtensa code,
and a target link does not qualify hardware.

Reproduce the focused checks:

```sh
bash test/run_scoped_provider_low_level_test.sh
bash test/run_scoped_provider_graph_module_test.sh
bash test/run_scoped_provider_module_test.sh
bash test/run_native_bank_provider_admission_test.sh
SANITIZE=1 bash test/run_scoped_provider_low_level_test.sh
SANITIZE=1 bash test/run_scoped_provider_graph_module_test.sh
SANITIZE=1 bash test/run_scoped_provider_module_test.sh
SANITIZE=1 bash test/run_native_bank_provider_admission_test.sh
bash test/run_provider_graph_v2_test.sh
bash test/run_provider_module_lease_v2_test.sh
bash test/run_app_stream_sessions_test.sh
bash test/run_runtime_test.sh
bash test/run_image_cache_pressure_test.sh
pio run -e esp32s3 -e esp32s3-16mb-paired -j 1
```

On this executor, LeakSanitizer cannot run under ptrace; the local sanitizer
commands use `ASAN_OPTIONS=detect_leaks=0`. ASan/UBSan still run. CI retains its
normal sanitizer configuration. Terminal retained-custody cases run in separate
processes and deliberately keep their exact resource owners reachable.
