# Default-off nonblocking settings availability

This X4-only experimental binding leaves all feature selections off. Watch has no
SD and is not admitted. Existing KV1/KV2 and atomic-v1 layouts are unchanged.

## App contract

`RiscKeyValueAvailabilityV1.h` defines `storage.key-value.availability@1`. The
manifest must require this capability and boot must grant exact namespace 1
(preferences) or 6 (Wi-Fi). A grant exposes only RAM availability and generation;
it grants no data access, source identity, path, reset, merge, or write authority.

- `UNAVAILABLE`: native storage lacks an authoritative root and discovery has not
  found an admissible source. KV read-through returns NOT_FOUND for temporary
  in-memory app defaults. Atomic state/get/submit return BUSY; no defaults persist.
- `RECONCILIATION_PENDING`: a complete source has been collected, but its data is
  private until the owner boundary has no live data grants for that namespace.
- `AVAILABLE`: native settings are authoritative. It does not promise an SD copy.
- `CONFLICT`: discovery found conflicting or rejected sources. Preserve them and
  show the unavailable state; no automatic source choice, merge, or reset occurs.

Generation is a monotonic RAM change counter in one Runtime lifetime. Consumers
should show unavailable/pending/conflict explicitly, preserve RAM edits, and
refresh a clean view only after releasing all relevant data grants and observing
AVAILABLE. Status-only grants do not fence publication. Legacy KV, atomic KV,
resident host/foreground, and live bound-provider KV grants do. A bound provider
can hold its grant for the entire Runtime lifetime: a clean restart may then be
necessary. Merely navigating Home is not proof of a reconciliation boundary.
Never release a draft's data grant as a way to discard edits silently. If edits
conflict with recovered values, ask the user whether to discard the temporary
edits and reload; saving/merging those edits over recovered data is not implied.

## Owner lifecycle and custody

Hydration reads native roots on the worker and boots with unavailable namespaces
when roots are Missing/Initializing. A corrupt native root still fails closed.
SD discovery is absent from boot hydration and eager/cold provider activation.
The first app-entry yield can start one discovery sweep. Later owner checkpoints
or cooperative yields poll one step without scheduler-draining SD discovery.
A clean failed sweep retries no sooner than 1,000 ms; this delay is not an I/O
cancellation deadline. Core.ready() mid-worker is never authority.

Only terminal collection plus complete custody release can mark a source pending.
Publication then occurs inside Runtime's owner checkpoint, without callbacks or
I/O, after its exact namespace data-grant fence. Native root initialization uses
only separately checked source authority; ambiguous absence never authorizes
persistent defaults. Healthy initialized namespaces remain usable without SD.

An accepted discovery owns permanent clients and its own graph pin. Narrow
app/UI continuation predicates allow that exact healthy custody, preserving all
unrelated retention/lock/USB/HCI/HTTPS checks. Direct HTTP/TCP/entropy/radio work
returns Busy during the owned SD pin; Files and USB retain their original raw
custody refusals. Global exitSafe, restart, update and storage teardown barriers
are not weakened. Runtime cancels and collects accepted work before graph shutdown.
Unrelated Runtime retention freezes discovery, preserving its graph pin and all
uncollected custody. No timeout, cancellation result, or UI transition fabricates
safe cleanup of a stuck hardware call.

Normal initialized-native maintenance retains its original scheduler-only drain
and atomic-draft fence. The nonblocking exception applies only to discovery.
