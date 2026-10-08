# Immutable installed app image bytes

Runtime 0.1.65 keeps a bounded byte cache for the configured default and exact app
policy paths during one `Runtime::run()` session. Repeated launches reuse their
immutable file bytes and the successful structural parse already performed on
those bytes. There are no file-stat, digest, unchanged-file or corruption probes.
Installation/update admission is unchanged. The first read on a cache miss still
performs the loader's existing structural safety parse; a hit does not repeat it.

Every launch still allocates independent executable/data/BSS sections, resolves
imports in that invocation's existing context, relocates and publishes the new
mapping. Export names are copied into mapping-owned storage. Normal init, entry,
fini, grant revocation, app allocation cleanup and unload run as before. Raw
input bytes never keep an invocation alive or substitute for a live mapping.

## Ownership and replacement

The cache is private firmware plumbing, absent from ELF import tables and public
SDKs. It is allocated only after successful eager provider activation, belongs
to the serialized Runtime owner, and keys the full exact prepared path within
that one session. Loose child paths use the ordinary reader. Providers retain
their existing independent-mapping and explicit session-retention behavior.

The existing boot store contract is immutable for a running session. App and
cohort updates write the inactive bank. Activation selects the next boot;
`NativeBankStore` never remounts/replaces the active store under a running app.
Restart creates a new Runtime/cache even when the selected bank uses the same
`/bootfs` mount path. No persistent cache crosses that explicit lifecycle.
Cache entries are also destroyed on every normal/error/retained return from
`run()`. A retained app/provider mapping remains valid because it owns its data,
code and symbol names independently. No new store-generation protocol is needed
for the current replacement model. A future in-place store replacement must end
the old session and its cache before reusing a path; it cannot silently reuse this
contract for mutable stores.

## Bounds and allocation fallback

The cache holds at most four recently used entries and 1 MiB of payload in total.
It takes ownership of an already-read buffer without copying it. New insertions
evict the oldest entries as needed. Larger images still load normally and are
not cached. Input reads retain the existing 8 MiB cap and 30-second deadline;
[byte/time checkpoints](ELF_READ_CHECKPOINTS.md) bound cooperative yields.

Native caching is enabled only with the loader's PSRAM allocation configuration;
non-PSRAM targets take the uncached path without allocating a cache control block.
The ESP32-S3 control block is 1,064 bytes in PSRAM, plus a four-byte Runtime owner
pointer. Payload and mapping allocations remain separate. A cache allocation
failure leaves ordinary loading available. A loader allocation failure releases
all cached payloads and the control block, clears the owner pointer, and retries
that failed allocation once. Relocation retries only an allocation failure after
freeing its partial mapping; other failures are returned without retry. Failure
to allocate the next app's allocation ledger similarly discards the cache before
one retry. That session stays uncached after reclamation. This is a bounded
optimization, not a general-purpose memory manager or a guarantee that an app's
working set will fit.

## Deterministic evidence

`test/run_image_byte_cache_test.sh` uses production `dlfcn.c`, `dlmod.c`, the file
reader, structural validator and section/symbol relocation code with real Xtensa
ELFs. Only RTOS, allocator, architecture relocation and publication are replaced
on the host. For default/app/default/app/default with the repository's two small
fixtures:

| Operation | Uncached | Cached |
|---|---:|---:|
| File opens | 5 | 2 |
| Seeks | 10 | 4 |
| 4 KiB read calls | 5 | 2 |
| File bytes read | 12,376 | 4,876 |
| Whole-image structural parses | 5 | 2 |
| Fresh relocations | 5 | 5 |
| Code publications | 5 | 5 |

The two hits returning to default and the repeat child launch perform zero file
reads or structural revalidation. This proves work elimination, not device
latency. The suite also checks independent data/BSS and export lifetime after
cache destruction, allocation-failure cleanup, allocation-only fallback,
non-PSRAM fallback, LRU/byte bounds, exact same-basename paths, oversized images,
and malformed/missing input handling.

The same production-loader measurement using the existing X4 paper-stage 0.1.15
default (301,772 bytes) and springboard (185,748 bytes) builds gives 314 to 120
4 KiB read calls, 1,276,812 to 487,520 file bytes, and five to two whole-image
parses over the same five launches. Relocations/publications remain five. This
is 789,292 fewer flash-backed file bytes requested (61.8%). Both entries fit
together under the bound; workloads exceeding it can evict and reread images.
Set `ELF_BENCHMARK_DEFAULT` and `ELF_BENCHMARK_CHILD` to two real admitted ELF
paths when running the loader test to reproduce those operation counts for
other inputs that fit the cache. This host run still does not time flash hardware.

`test/run_image_cache_runtime_test.sh` executes real host app init/entry/fini
through the production Runtime and module registry, with host mappings taking
immutable input snapshots. It checks five fresh invocations with two reads,
unload-before-next-load, unlisted child paths, missing-child fallback, failed
init, retained app mapping custody with zero remaining input buffers, and a new
store session replacing bytes at the identical path. Host mapping/relocation is
not Xtensa execution, PSRAM/cache/DMA emulation or hardware qualification.
