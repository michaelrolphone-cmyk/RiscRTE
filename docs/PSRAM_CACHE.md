# Device-scoped PSRAM writeback

`platform.psram-cache@1` is an explicitly selected device dependency, not an app import or allocation service. Its versioned table exposes cache-line bytes so consumers can reserve stable storage and padding for every intersecting line. The caller owns a valid live allocation; address-range admission cannot establish allocation ownership. Keep contents stable through writeback and subsequent DMA completion. Failure or partial writeback forbids starting DMA.

The native ESP32-S3 binding uses the pinned SDK public data-cache writeback and patched CACHE-126 path. It is unavailable unless both instruction and rodata PSRAM XIP options are enabled: the SDK excludes concurrent flash operations otherwise. Production defaults are not changed. Calls require a running task outside ISR/critical sections and scheduler suspension.

Requests are nonempty, at most 1 MiB and wholly within PSRAM. SDK calls are at most 4096 bytes; yielding occurs after 32768 bytes or 2000 microseconds. Clock rollback and the cooperative 100 ms deadline refuse continuation. SDK locking/ROM polling cannot be preempted here: this is **not a hard completion-time guarantee**. The SDK may expand unaligned requests to adjacent cache-line bytes; requested-byte ownership alone is insufficient.

## Qualification

`python test/run_psram_cache.py --output receipt.json [--sanitize]` compiles actual core, native adapter and Runtime/CpuPort selected-graph admission. It records all repository dependencies using `-M`, separately hashes compiler-standard headers and rejects include-environment overrides. Eight additional cases exercise the real diagnostic source with modeled DMA/cache behavior; these are not physical visibility evidence.

`test/native_psram_cache_target` is a standalone, test-only IDF diagnostic. Its explicit octal-PSRAM/XIP configuration is not a board compatibility claim. SDK async memcpy automatically flushes the source, so ordinary successful copies cannot qualify this wrapper. The diagnostic suppresses only the exact source/range/flags in the synchronous submit phase on the submitting task. All other SDK operations remain forwarded; unexpected overlap/interception refuses qualification. A stale negative transfer is required before wrapper writeback and a matching positive transfer. Eviction makes the result inconclusive, not passed.

The target verifier requires real Xtensa ELF/object/configuration evidence, pinned SDK sources, complete dependencies for three relevant translation units, actual wrapper call sites and patched writeback symbols. Build/link qualification is not device execution. A device run must retain the printed source commit and actual negative/positive transfer evidence.

The diagnostic deliberately retains its DMA handle and buffers on every terminal path. Observing the callback completion store does not prove ISR return or cross-core uninstall quiescence. Its strongest result is `visibility_passed retained=1 cleanup_qualified=0`, never whole-lifecycle success. No production link wrapping or new DMA engine is introduced.

## Composition

This additive increment is based on resident-worker commit `602b4a389726728815e8e7d49b8d5f905a062f8c`. Existing worker ownership, sleep fences, cancellation/join/release and storage contracts are retained. Cache writeback does not grant worker lifetime, DMA-idle, IRQ-idle or unload authority. Firmware version advances to 0.4.17 as required by the existing source-version guard.

Physical DMA visibility remains unobserved until an authorized compatible target executes the diagnostic. Host and hosted target builds do not close that criterion.
