# Retained stream notification under queue contention

Runtime 0.1.75 fixes a provider-wide authority gap without changing the stream
provider ABI, queue limits, package selection or physical transport behavior.
The base is canonical 0.1.74 `858ec7160212e0e0f3e891c0b70dfe61272cb2bf`.

## Failure and resulting behavior

Previously, `finish(context, endpoint, RETAINED)` tried the registry lock before
fencing the provider context. If both RX and TX notices returned BUSY, a locally
retained provider could stop permanently while its app still drained queued RX
and added TX bytes. Subsequent provider polls did not repair that lost fence.
The actual CDC/host/app-session reproduction is preserved separately in Reader
`9df9a473f2ed2bf3aa73d4fac2d59c61790a09c1`; its CDC implementation is
`b82d40e3da00f0985c3a8f47d5b08cc1a1b848b6`, with host
`d780fcbc93d5472eaa0dbc3f153b2f32f0c798e3`.

Now a valid retained notice atomically claims its live endpoint authority and
fences that exact provider generation before attempting the queue lock. The
metadata attempt can still return BUSY, but `safe()` is already false. Graph
polling/admission stops, app session validation fails, and new app queue reads
and writes are rejected with zero bytes. Queue bytes, mapped code and physical
custody remain retained. No provider retry or repeated notification is needed.

The endpoint still matters. A wrong-owner endpoint returns DENIED when it can
be checked; a missing, closed or stale endpoint returns CLOSED; malformed input
returns INVALID; lock contention retains BUSY semantics. None of those invalid
identities can fence a different provider or a reused context generation. EOF
and ordinary negative terminal behavior remain unchanged.

## Atomic ownership mirror and ordering

Each Context has four atomic 32-bit endpoint-ID slots, matching the existing
four-queue limit. Queue metadata records which slot belongs to that queue.
Slot allocation uses the registry's live queue records, not whether a mirror
word temporarily equals zero. A notification may have claimed a word while the
queue is still live; that word cannot be reused for another queue.

Publication installs complete registry metadata and release-stores the nonzero,
monotonically issued endpoint ID before returning it. Zero remains the existing
invalid ID; no additional sentinel or token is reserved. `UINT32_MAX` remains
a valid final endpoint ID, and the next publication fails on exhaustion.
Context generations retain their existing `UINT32_MAX >> 2` maximum.

A RETAINED notification performs a bounded scan of its four ownership slots:

1. Acquire-load the matching endpoint ID.
2. Validate the exact current Active context generation **after** that load.
3. Compare-and-exchange that same endpoint ID to zero, competing atomically
   with endpoint closure.
4. CAS the matching generation's gate to Retained. An operation admitted while
   Active can finish that transition from Revoked if revocation raced with it.

The ID-before-generation order is necessary. A stale caller paused before slot
reuse could otherwise consume a later occupant's new ID. A new generation
publishes its gate before publishing its IDs, and IDs never repeat: the acquire
load plus subsequent generation check reject that case. If close/reuse occurs
later, the endpoint CAS or exact-generation gate CAS fails safely.

A metadata-only ownership lookup followed by a gate CAS would also be unsafe:
close could retire the endpoint between those actions. Notification and closure
therefore compete to withdraw the SAME atomic ID. A notifier that wins the ID
owns the right to fence; registry-locked cleanup that encounters a missing live
ID helps fence that same validated context and aborts cleanup. It cannot
silently destroy the queue while the winning notifier is paused before its CAS.

Reserved endpoint close withdraws its ID and marks the queue closed while
preserving its bytes and reservation. Confirmed pair retirement withdraws both
remaining IDs before destroying either queue. Already-closed reserved queues
need no second withdrawal. Closed endpoint notices cannot regain authority.

Whole-context destruction first withdraws all still-live endpoint IDs. It then
CASes that exact Revoked generation to a private Retiring state (nonzero
generation with zero state bits). If any notifier won an ID or already fenced
the generation, retirement fails with custody intact. Once Retiring is entered,
no endpoint in that generation can authenticate a new retained notice. After
destruction, only a CAS from that exact Retiring word can mark the slot free;
there is no unconditional zero store that could overwrite Retained. A later
occupant always has a new generation.

## In-flight operations and task authority

The fence denies new authority; it does not preempt a bounded operation already
admitted under the registry lock. A transfer admitted before the fence can
finish copying its bounded chunk and report its actual byte count. A publication
that passed its final Active admission can finish publishing a tracked queue
and return its ID, even if another existing endpoint fences the context in that
interval. That queue remains recorded and retained; it cannot be used afterward.
A publication fenced before that admission returns CLOSED without an ID or
committed allocation.

An individual close that wins the endpoint withdrawal can complete its already
admitted closure. A notifier arriving after that withdrawal has no authority
through the closed ID. A notifier that wins first prevents that endpoint's
closure. Destruction of other endpoints already admitted for checked cleanup
can complete; remaining queues and the whole provider generation stay retained.
This does not relax the provider's existing physical-before-queue cleanup duty.

App APIs keep their owner-task, active invocation, graph grant, direction and
stale-handle checks. Provider stream callbacks already support provider workers
using their opaque context; they do not acquire an app owner-task requirement.
The mirror and gate use confirmed lock-free 32-bit atomics, with no waits,
allocation, ELF callbacks or graph lifecycle reentry in the notification fence.

## Memory and ABI cost

Measured with the existing Xtensa S3 compiler and ordinary host compiler:

| Layout | Before | After | Change |
| --- | ---: | ---: | ---: |
| One Context | 4 B | 20 B | +16 B |
| 17-context plain target | 68 B | 340 B | +272 B static |
| 24-context paired/host target | 96 B | 480 B | +384 B static |
| One Queue | 104 B | 104 B | 0 |
| 32-queue lazy metadata | 3328 B | 3328 B | 0 |

The queue's slot index uses previous alignment space. Queue byte limits and
allocation timing are unchanged. Test interleaving hooks are compiled only with
`RISC_STREAM_HOST_TESTING`; production gains no callback table or test API.
The public SDK headers and existing ABI prefixes are byte-for-byte unchanged.

## Verification

```sh
bash test/run_provider_queue_host_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_provider_queue_host_test.sh
bash test/run_provider_queue_retained_fence_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_provider_queue_retained_fence_test.sh
bash test/run_app_stream_sessions_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_app_stream_sessions_test.sh
```

The focused mirror suite uses deterministic hooks in actual production queue
code, not only scheduling races. It covers both winners of notification/close,
reserved pair and context retirement; notification paused before the gate CAS;
malicious stale context/new endpoint reuse; publication before/after admission;
in-flight byte counts; wrong/stale/closed/foreign endpoints; worker notification;
retained storage; and unchanged endpoint/generation exhaustion.

The generic Runtime integration adds `terminal-retained-busy` in eager and
demand modes: it loads real app/provider images through production Runtime,
Graph, Module, AppStreamSessions and queue dispatch; both terminal notifications
return BUSY; after unlock both app reads and writes are denied, no cleanup or
fini runs, and provider/dependency/app mappings remain retained. The separately
preserved Reader reproduction supplies the actual CDC/production-host comparison.

Address/undefined sanitizers are used. Leak detection is disabled because retained
fixtures intentionally keep mappings and custody (and the execution environment
cannot reliably run LeakSanitizer under its tracing). Host tests, target objects
and firmware builds do not qualify USB, PHY, VBUS or physical-device behavior.
