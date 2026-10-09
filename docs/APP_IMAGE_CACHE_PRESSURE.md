# App-image cache pressure and composition policy

Runtime 0.1.67 makes input-byte caching explicitly opt-in with
`RISC_APP_IMAGE_CACHE=1`, default zero. All existing target definitions leave it
off. The 0.1.65 cold-reader byte/time checkpoints remain independent and active.
The four-entry/1 MiB PSRAM cap, exact immutable store/path identity, fresh app
state and explicit session/restart invalidation remain unchanged.

The earlier cache reclaimed only during its own app loading path. Its retained
bytes could therefore cause a later provider or app-owned allocation to fail
even when the same workload fit without caching. The pressure hook now gives
those covered allocations one opportunity to release the optional cache. It
does not obtain extra authority or transfer allocation ownership.

## Covered allocation paths

- The production `esp_elf_malloc` adapter, including provider mapping allocation.
- App-owned malloc/calloc/realloc, heap-capability allocations, and the next
  invocation's native allocation-ledger creation.
- Existing provider libc malloc/calloc/realloc imports, through the production
  symbol resolver. They remain independent of the app ledger and keep ordinary
  free/realloc ownership semantics.

Only a failed allocation can request reclamation. Success does no cache work.
The request checks the compiled-in owner task before accessing Runtime, detaches
the callback and cache pointer before freeing, and disables reuse for the rest
of the session. The same request retries once. A second failure propagates
normally. Zero-size disposal, overflow, lock/context rejection and ledger-slot
exhaustion do not cause retries. A failed realloc retains the original pointer
and its ledger record.

Mapping owns its relocated sections/export names. A cache hit transfers the
input buffer to a local load before allocations can invoke the pressure hook.
Consequently reclamation cannot free the input currently being relocated, app
globals, provider state or retained mappings. No filesystem, hash, freshness,
corruption or free-heap checks are added.

## Default-off boundary

The hook is private firmware plumbing, absent from ELF imports and capability
tables. It is not global allocator interposition. Allocations inside native SDK
services or other paths that do not use these wrappers are not covered. Foreign
tasks cannot evict the owner cache. An opt-in target still needs headroom or
separate qualification for those paths; an unqualified cohort should leave the
flag off. The production default creates no cache, registers no pressure owner,
and keeps the original provider libc allocator addresses.

## Production-path host regression

`bash test/run_image_cache_pressure_test.sh` builds both flag values using the
actual Runtime, provider graph, dynamic module registry, native app ledger,
target ELF allocation adapter, and symbol resolver. Only the host mapping,
bounded heap, log sink and RTOS operations are substituted. Host app/provider
bridges use the production symbol resolver's allocator function addresses;
the tests do not duplicate its selection or retry implementation.

A 512 KiB immutable host app input is held in the enabled cache. The test then
applies the same 384 KiB budget to the measured allocation paths for both flag
values and requests a 256 KiB app/provider working allocation. This is controlled
host pressure, not a measurement of a physical Watch's complete memory use.

The 24 cases cover:

- Provider mapping pressure and pressure inside provider start: one failed
  allocation with caching, successful reclamation/retry, and no failure with
  the flag off under the same budget.
- App malloc, calloc, realloc and capability-backed allocation, with zeroed
  calloc contents and preserved realloc contents.
- A retry that still cannot fit: exactly one retry while reclaiming; subsequent
  failures do not retry, and the original realloc pointer remains owned.
- Actual foreign-thread rejection and reentrant attempts during deallocation.
- A cached-hit allocation failure after its input has been detached: destroying
  the cache leaves that input alive, and the second invocation needs no reread.
- Next-invocation ledger allocation failure while previous input bytes are held.
- Provider allocations surviving app-ledger cleanup, normal provider stop, and
  failed provider quiescence retaining the provider mapping/state.
- Retained app allocation/mapping custody with the cache already detached.
- Zero-size/overflow behavior, clean session exit, and no new import authority.

Normal and ASan/UBSan modes use the same production paths. The sandbox runs
disable LeakSanitizer because the environment uses ptrace; the harness directly
asserts zero remaining tracked allocations/mappings on every normal exit.
Retained cases deliberately preserve their tracked allocations and mappings.
These results are software evidence, not hardware memory qualification.
