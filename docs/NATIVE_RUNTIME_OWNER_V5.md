# Default-off native v5 Runtime owner callers

This additive successor is based on Core
ec86b26b2f06271c62405dc8976b07dae19b713f (the v5 authority prerequisite).
It adds actual selected eager/cold Runtime startup and checked native close.
The native cohort remains explicitly opt-in. ABI3 and ABI4 remain unsupported;
ordinary and ABI1/ABI2 builders, inventories and descriptor behavior remain
unchanged. Firmware source version 0.4.18 is routine source metadata, not a
release or deployment.

## Authority and startup

Runtime's existing grants array retains the actual occupied grant. The
feature-guarded, noncopyable/nonmovable NativeOwner additionally binds that
exact slot/generation to its owning Graph, Graph-owned copied policy and full
mapping. Actual private stage/record logic runs before mapping and commit.
Clean refusal, unsafe grantless stage, mapping failure and sticky commit
failure remain distinct; no record is cleared merely because a step failed.
Dependencies, policy/config and code remain alive when custody is retained.

Only selected eager/cold startup chooses the native stage/map/commit path.
Ordinary acquire never becomes an implicit native grant. Demand activation,
cold promotion safety, app-exit barriers, storage safety and all existing
owner gates remain in force. Commit opens worker entry permission, not
quiescence or shutdown permission.

## Optional terminal suffix and checked close

RiscNativeTerminalV1.h adds an explicitly tagged/versioned/full-size suffix
after the frozen NRV1 prefix. A fixed signed 32-bit callback result represents
unknown values safely so they can be rejected. Exact full descriptor size is
checked before reading any suffix field. Table version, complete size and
non-null callback are checked. This parsing applies only to selected ABI5;
a larger native ABI2 NRV1 remains uninterpreted as before.

A committed owner may request a provider-neutral prepare-close callback under
the exact held grant/policy/mapping. The implementation conservatively permits
one callback attempt, including refusal or unknown result, and has a
same-record reentrancy guard. Acknowledged or Pending means only that the
request was accepted. Subsequent Closing readiness checks never call it again.
Actual nativeTaskSafe, lease/stream checks, descriptor quiesce/stop and exact
checked release are still required. Missing suffix permits ordinary safe
release only if the legacy worker already completed; it invents no stop opcode.

An uncommitted staged owner uses actual staged cancellation once while its
grant remains occupied. Pending preserves the record. Later readiness calls
only actual worker join and rechecks authority/dependencies/mapping. Only an
actual safe tail permits the first release. All Module preparation callers
skip redundant cancellation after actual safety, retaining every subsequent
revoke/lease/quiesce/stop/unmap gate.

Any release(false) is ambiguous as to pre/post-revocation state and becomes
sticky Retained. There is no release, cancellation or terminal-callback retry
after it. Identity mismatch refuses without affecting an unrelated grant.
Successful release alone clears the record. Runtime's outer cleanup is
bounded and one-pass: a still-pending owner retains storage and requires
restart; this is not an automatic eventual-shutdown loop. The lower checked
helper can observe a later safe tail on the same Closing record without
reissuing an accepted request. Fatal retained destruction aborts before
Runtime-owned policy/config and Graph members are destroyed.

## Hosted evidence and reproduction

The final private source/fixture snapshot passed 34 actual Runtime-caller
cases normally and the same 34 with UndefinedBehaviorSanitizer, including
immediate/delayed/failed staged cancellation. Its normal report SHA256 is
2fdd8a36aee876de92060136f8074a8abed82281be404b1cf3be04d5074f60ef;
UBSan report SHA256 is
601fb70fe66cef01f980de157b22a505bcdb98b854371f86225a6ddd967e6a42.

The unchanged existing v5 hosted runner also passed its 20 normal/UBSan cases
after exact owned-source pin refresh. Its result SHA256 is
6dc538e9d463c7f2bd6e9f6f6ed40d259a9a62c9793120a49295edac9d96e30b.
Default, non-native/candidate-on and native/candidate-off profiles all exclude
new owner/terminal storage and test-peer friendship and match ec86 sizes on
the measured arm64 host: Runtime 288152, Module 680, Graph 45376, NRV1 104.
These are host measurements, not target ABI sizes.

The unchanged ordinary Runtime recipe has the same existing Darwin failure
on ec86 and the final successor: six groups pass, then runtime_test.cpp:328
fails its waits-vector assertion. No assertion/timing guard was weakened and
no complete ordinary-suite pass is claimed. Combined controls report SHA256:
0cf969f8d7e0af50374bfbce925b05a44f9806df970b66f3b19ea3ddc02ea3cd.

The repository-relative reproduction entry is test/run_native_runtime_v5.py;
its exact commands/cases and boundary descriptions are under
test/native_runtime_v5. The existing macOS hosted workflow runs both owning
entries with explicit xcrun and existing OpenSSL paths, without installing
tools. Qualification receipts remain separate from source pins.

## Explicit limits

The hosted suite links actual Runtime/Executor/Graph/Module/resident worker
code against explicit modeled ELF/kernel/scheduler/physical-retirement and
export leaves. Tiny host app/dependency fixtures are not native firmware.
The tests perform zero network I/O. Test peers exist only under their explicit
test macro; there is no new public stage authority.

The physical internal-mapping adapter remains unavailable/fail-closed. This
does not prove target compilation, physical retirement/cache safety, internal
memory availability, TLS placement, a complete native owner, operational
activation or deployment. Those are separate gates; no protected target
settings or runtime fallback was changed.
