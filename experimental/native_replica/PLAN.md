# Native replica / explicit-operation checkpoint

Status: **unconnected host prototype**, based on Runtime
`3b90cd11cf0ff4e8116bb54d2d9eb9c7c67a148b`.
Nothing in `.90` boot policy, Runtime routing, production storage, SDK, source
version or build recipes references this directory. Not enabled in any product build.

## First independently testable checkpoint

`NativeReplicaCore.h` owns a complete native KV image and an ordered list of
explicit pending operation groups. The map is authoritative for native reads.
A group changes its keys and records its opaque application intent atomically.
DELETE of an absent key and PUT of the currently displayed value remain explicit
operations. Their purpose must not be inferred by comparing cached values.

Native operation sequence and SD checkpoint generation are separate counters.
An unknown base has no lineage/generation authority. Supplying a high-generation
SD candidate never silently discards local operations. Installing a resolved
candidate requires an exact local revision and the ordered ID of every pending
group. These IDs are an assertion by the authorized application resolver; the
core deliberately cannot prove application-level semantic correctness.

Acknowledgement removes only the groups captured by a live snapshot ticket.
It does not remove any native map values and cannot remove a later operation.
The checkpoint transport must independently verify the exact captured map,
remote identity and successful checked close before calling acknowledgement.
Tickets expire on reload and successful base changes. A transport needing to
resume acknowledgement after restart must reacquire and verify a checkpoint;
it cannot reuse a RAM ticket. This prototype has no SD transport/export API.

### Native durability contract

Backend reserve returns a never-reused immutable object ID. The candidate image
is written and read back in chunks of at most 4096 bytes. Only afterward is a
48-byte root record atomically replaced and read back. A failed root switch or
root readback is ambiguous: the core returns Uncertain and requires reload.
It never translates uncertain writes into a retry-safe BUSY result.

The backend must guarantee each root replacement leaves the old or new complete
root; a torn root is not assumed recoverable by this prototype. Candidate objects
may tear. No object reachable from the durable root may be reclaimed. Orphans
from interrupted reservations require backend garbage collection, which is not
implemented here. Old active objects are retired only after verified publication.
CRC is accidental-corruption detection, not authentication.

Current image fields include namespace, schema, native revision/sequence,
acknowledged sequence, known/unknown base identity, complete map and pending
groups. Each group retains its sequence, original base identity, mutation list
and opaque semantic intent. App values remain 1..2048 bytes and key grammar is
unchanged. DELETE is internal to this prototype, not a change to the live KV ABI.

### Resource and failure behavior

Limits are explicit image bytes, per-group bytes and intent bytes. There is no
sixteen-key namespace ceiling. The default image bound is 1 MiB; tests select
4 MiB. These are prototype resource limits, not approved product capacities.
A 4096-byte group can contain many small edits or one 2048-byte value plus
metadata. Wi-Fi's count/chunks/selector/identity edits fit as a small group.

The core holds an active image plus one candidate image and a 4096-byte stack
readback buffer. Thus internal peak allocation is at most twice imageBytes plus
4096 bytes, excluding caller inputs and backend buffers. Allocation failure,
invalid input, stale revision and capacity failure before root publication leave
the previously committed state unchanged. Space is reserved for the whole
candidate before publication; never delete the old replica to make room.
A native failure can still make storage uncertain/unavailable; “native fallback”
is not a promise to fabricate defaults after native corruption.

This reference implementation rewrites a whole image per operation and uses
linear scans / duplicate validation. It is intentionally not a target flash-wear
or owner-task latency implementation. A production adapter needs incremental
work, page/record reuse or compaction, bounded owner polls, measured NVS capacity
and explicit reserve for recovery and garbage collection. It must retain the
same tested publication invariants. It must also wipe sensitive scratch buffers.

## Proposed backward-compatible interface, not implemented

Do not modify the layout or semantics of `storage.key-value@1/@2`, and do not
reinterpret an old `put` as proof of an explicit user action. Native replica
activation requires an explicit product option and a writer audit. Read-only
legacy consumers may continue to use their existing get table. Legacy writers
on an unknown base must be rejected or explicitly classified as unresolved;
never silently merge their default writes into an inserted card.

The smallest useful first ABI addition is an explicitly requested KV API 3,
with the existing get/put prefix and a size-guarded suffix:

- Read-only advertised limits and support flags.
- `commit_group(expected_revision, mutations, count, intent, intent_size, result)`.
  It atomically commits the complete mutation batch and opaque semantic intent.
  It returns native revision and operation ID. No begin/put/commit transaction
  handle is necessary: a single bounded request avoids abandoned write custody.
  PUT and DELETE have explicit tags. Same-value explicit edits are retained.
  A revision mismatch has a distinct stale result and starts no mutation.
  IO retains uncertain-commit semantics. This callback only proves native
  durability, never SD durability.

The v3 prefix still reports v3 and requires explicit v3 admission; it must not
be blindly handed to clients checking api_version == 1. Existing codecs can
receive an app-owned adapter for staging their get/put calls into one batch.
No additional namespace or filesystem authority is granted. A provider-bound
variant, if needed, must validate every batch key against the existing exact
read/write key map; do not expand provider access to an entire namespace.

That single commit callback is sufficient for explicit atomic local intent,
**not** for cross-base reconciliation. Before automatic card-return merging,
the same namespace-scoped v3 suffix (or a separately admitted resolver view)
needs these distinct logical operations:

1. Open a read-only/staged rebase view with remote identity, local revision and
   captured pending frontier. No raw SD handles or caller-supplied namespaces.
2. Read a key from that view and enumerate pending opaque intents by cursor.
3. Apply a bounded resolved batch to the staged view for an identified pending
   operation. This can reuse commit_group's batch format; resolution records
   must not create a new user intent or consume the original intent prematurely.
4. Finish or abandon the view. Finish compares the native revision/frontier and
   atomically switches the complete candidate; original intents remain pending
   until the resulting SD checkpoint is acknowledged. A concurrent operation
   makes finalization stale rather than allowing silent overwrite.

Avoid compressing these operations into an opaque generic syscall merely to
minimize the callback count. Specify the view lifecycle/revocation, copied data,
limits and statuses before freezing an ABI. This checkpoint implements only
revision-checked resolved-candidate installation, not that live view ABI.

## Wi-Fi reconciliation stays in System Apps

Create an app-owned, versioned intent codec outside Runtime. An intent describes
Save/Forget using logical profile identity, desired values and an expected record
fingerprint when available. A numeric profile slot is never persistent semantic
identity across an unknown/different base. Existing SSID-based lookup can be the
first mapping rule, but duplicate SSIDs and a changed expected record must become
an explicit conflict; do not guess which profile to replace/delete.

An app-owned staging adapter runs the current credential codec in a bounded
memory overlay, then commits count/reservation, credential chunks, selector and
any identity metadata in one native operation group. Interrupted staging has no
external effects. The resolver scans remote profiles incrementally, finds the
logical target, remaps or allocates a slot, regenerates the count/chunks/selector
and emits one complete resolved group. Forget retains its committed tombstone
and cleanup semantics. Never blindly copy an offline wifi.profiles count or
partially written chunk over a newer SD collection.

The resolver may run when the Wi-Fi app is explicitly launched, or in an
explicitly bound System Apps resolver component. It receives only namespace 6
views/intents. Runtime stays schema-blind. Reconciliation must not auto-connect
Wi-Fi or log credentials. If no resolver is available, native reads continue and
SD synchronization remains pending/conflicted. No semantic merging is implemented
or qualified by this core checkpoint.

## Migration and integration gates

1. **Unselected .90 remains unchanged.** Keep this core disconnected. Its tests
   can run without modifying the SDK, boot configuration or production behavior.
2. **No existing format-2 anchor:** enumerate original native rows into a staged
   complete replica. Record this as imported baseline, not user intents. Verify
   and publish the native root before considering legacy cleanup. An absent
   replica and corrupt replica are different states; never initialize over a
   corrupt/present root.
3. **Existing format-2 anchor:** old native rows plus surviving deltas are not a
   complete replica. Require a validated SD map and old journal recovery first.
   If the card is unavailable, defer migration; do not revive stale native rows.
   Never delete old journal/anchor data until the new complete native root is
   durable and the transition recovery marker has been verified.
4. **Fresh erase:** explicitly initialize an empty unknown-base replica. Expose
   native missing values without writing defaults. Accept only explicit new
   intents from audited clients. On card return, preserve all intents and use
   the app resolver over the validated SD base, regardless of numeric SD age.
5. **Lineage:** use dataset lineage plus checkpoint digest and generation, not
   just a physical card identifier or generation. Same-generation/different-
   content, foreign lineage, rolled-back checkpoints and ambiguous card swaps
   must not be automatically published. Persist transition/reconciliation
   markers before any irreversible cleanup.
6. **Qualification before opt-in:** NVS adapter power cuts and GC, full-flash
   migration, actual SD worker cancellation/export, old ABI consumers, selected
   app/provider routing and launch readiness, Home/Alarm hydration and real
   Wi-Fi semantic reconciliation. New build receipts must distinguish this
   feature from the current shared-SD candidate. Preserve private namespaces.

## Test checkpoint

Run `test/run_native_replica_core_test.sh` and repeat with `SANITIZE=1`.
The deterministic backend injects returned errors and simulated immediate power
cuts before/after every operation, including torn candidate writes; root updates
respect the explicitly required atomic-record contract. Power-cut tests discard
all volatile allocations before reconstructing the core from durable bytes.

Coverage includes 1025 opaque keys; explicit same-value edits; PUT/DELETE groups;
permanent full replica across acknowledgement and reboot; pending intent payloads;
newer operations surviving older acknowledgement; unknown-base preservation;
revision conflicts; foreign/rolled-back/divergent identity; corruption; resource
exhaustion; and atomic creation/apply/reconciliation/acknowledgement boundaries.
Tests are not an ESP32 NVS, card power-loss, target resource or hardware claim.
