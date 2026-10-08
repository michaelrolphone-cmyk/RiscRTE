# Generic app byte-stream sessions

Runtime 0.1.66 provides a generic, invocation-bound app session broker and a
bounded provider byte queue host. Runtime never interprets serial requests,
transport identities, USB, UART, board power, or controller ownership. The
software serial witness lives only under `test/`; it is not an installed backend.
No application, capability grant, driver, or product catalog is selected here.

## ABI and authority

`RiscRuntimeV1.h` appends `stream_client` after the complete `trace` suffix.
All prior offsets and API version 1 remain unchanged. The caller checks
`RISC_RUNTIME_STREAM_CLIENT_V1_SIZE` and supplies the size of a zeroed
`risc_stream_client_v1`. The getter copies a service table with a fresh,
nonwrapping invocation context. It grants no capability or endpoint authority.

The app passes its existing explicit capability grant to `open`. Runtime checks
its complete slot, generation, API pointer, owning invocation, graph grant and
provider mapping. Runtime calls the provider adapter itself and returns separate
opaque session, RX and TX handles. Apps cannot supply raw sessions/endpoints.
One session is allowed per capability grant. A second acquisition is necessary
for a second independent session.

`RiscStreamSessionProviderV1.h` appends to the complete provider poll prefix:
`extension_tag`, `extension_version`, then `stream_sessions`. The tag is
`RISC_DRIVER_STREAM_SESSIONS_TAG_V1` (0x53535631), version 1. Unknown tags are
ignored without reading adapter memory. A recognized tag with unsupported
version or truncated size fails admission before pointer access. The adapter
requires complete open/call/close functions, stream binding and quiescence.
Existing base/diagnostic/stream/poll provider prefixes remain supported.

Every method checks the active owner task and invocation. Handles also bind to
app grant generation, exact graph lease, mapped-provider context, provider token
and queue identities/direction. Nonowner, stale, forged, closed or cross-direction
calls cannot enter the provider. Graph generations and context/handle issuers
fail closed on exhaustion rather than reuse identifiers.

## Bounds and results

The public statuses preserve T5 values 0, 1, 2 and -1 through -10. `RETAINED=-11`
is additional. Requests and replies are copied through aligned Runtime storage,
at most 512 bytes each; no caller buffer is retained. Transfers accept at most
512 bytes per call. Counts are zeroed even on failure; positive partial progress
is OK, temporary empty/full is AGAIN, graceful drained receive is EOF, and
negative terminal errors reject transfers immediately. Counter additions saturate.
A successful write counts accepted bytes, not physical transmission or flush.

There are at most 32 live endpoints, four per mapped provider, 4096 bytes per
endpoint, 32 KiB total queue storage, 16 app sessions and 32 consumer handles.
Provider context slots follow `RiscLimits::Providers` (17 on the plain X4 target,
24 on the host and cohort/metadata-PSRAM targets). Record operations explicitly
return UNSUPPORTED. No pipes, files, HTTP, global buffer creation, arbitrary
endpoint grants, seeking or individual consumer endpoint closure are exposed.

The process-lifetime registry retains context gates and nonwrapping issuers.
Its fixed queue metadata allocates once on first provider stream-host open;
Runtime's fixed session ledger allocates on first supported app session open.
Queue byte storage allocates only when the provider publishes an endpoint.
Metadata-only Runtime candidates and legacy apps allocate neither queue bytes
nor session metadata. Allocation failure does not consume authority. All storage
is bounded host-owned allocation, independent of app allocation reclamation.

Short queue sections use a nonblocking try-lock. Checked host lifecycle hooks
propagate contention instead of pretending cleanup succeeded. Provider callbacks
run outside those locks. Graph acquisition, teardown, polling and session
callbacks reject nested lifecycle operations. A control callback receives one
positive budget of at most 1000 ms, covering all its lower operations. Runtime
measures overruns and retains the invocation; native execution is cooperative
and cannot be preempted by this broker.

## Transaction and retention

A provider open must create a fresh token. Runtime authenticates two distinct,
nonterminal byte queues owned by the exact provider context: READ-only RX and
WRITE-only TX. It reserves the pair and issues both grants before publishing any
app output. First/second grant failure revokes provisional authority and performs
one checked adapter close. Unauthenticated malformed/duplicate/foreign output
fences without closing a suspicious token that might belong to someone else.

Clean failed open returns no surviving payload/custody. Partial or uncertain
provider rollback returns RETAINED, even with token zero. Close revokes consumer
authority before its provider callback. Only definite callback success permits
queue/session retirement. Provider endpoint close preserves reserved queue
storage until Runtime confirms physical close; adapters must also preserve
unreserved partial-open storage until their own checked rollback succeeds.

Capability release closes its session before graph unpin. Failed physical close
cannot be hidden by an eager/demand-retained boot pin. Runtime cleanup stops at
the first uncertainty. Retained sessions revoke all copied client authority and
preserve app image/allocation, provider image, dependency tables and remaining
custody. They allow no fini, polling, child launch, cleanup retry or handle reuse.
Healthy sessions may close in fini or through automatic invocation cleanup.
Default/child/default reload always receives a new context.

## Portable serial integration and verification

`RiscSerialStreamSessionV1.h` contains only app/provider request layouts (32-byte
open, 24-byte call). Runtime does not include it. The provider validates canonical
inventory and exact device generation, framing and control-line values. Unknown
inventory returns IO and pauses data movement; a confirmed absence/reconnect
returns DISCONNECTED. Physical serial implementations must propagate the total
callback budget themselves; the software witness proves no USB timing bound.

`test/run_app_stream_sessions_test.sh` loads actual provider and app shared
images through production Runtime/Graph/Module and the production broker/queues.
It covers legacy prefixes, unknown tagged suffixes, copied stale tables, forged
grants, wrong owner/direction, independent sessions, partial/backpressure I/O,
first/second grant rollback, eager/demand/promoted lifetimes, default/child/init
failure, explicit/fini/forgotten close, reentry, copied bounds, slow callbacks,
malformed output and retained open/call/close/revoke/quiescence failures.

`SERIAL_SYSTEM_SOURCE=/path/to/System bash test/run_serial_stream_client_integration_test.sh`
compiles the real production PortableSerialClient with the canonical SDK and
loads its software serial provider through the same production path. It covers
one-byte pumping/partial writes, exact ordered bytes, accepted-byte accounting,
DTR/RTS/framing, unknown/malformed inventory and recovery, detach/reconnect,
EOF, cancellation, stalled-queue deadlines and retained rollback/close. Raw
prefix transfer calls are asserted absent. Both suites support `SANITIZE=1`.

`test/build_stream_target_witness.py --system /path/to/System --cc /path/to/xtensa-esp32s3-elf-gcc`
builds actual Xtensa client/provider ELFs and validates entries, imports, headers,
relocations and corrupted/truncated variants. It does not stage a boot default.

The byte-ring rights/copy semantics follow Reader `StreamRuntime` and
`NativeStreamBridge.p4.inc`; this implementation excludes their app, file, HTTP,
legacy serial, task and physical backend infrastructure. Existing Reader source
provenance and licensing remain intact. Passing host/Xtensa software checks does
not qualify physical USB/controller/PHY handoff, X4 VBUS/UART or transport power.
