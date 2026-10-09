# Bounded cold ELF read checkpoints

Runtime 0.1.65 retains 4 KiB maximum read calls and the existing 30-second total
read deadline. It replaces an unconditional `vTaskDelay(1)` after every chunk
with a checkpoint after 32 KiB or at least 2 ms of elapsed work, whichever comes
first. The tick threshold is at least one tick on lower-frequency ports. The
reader still checks the total deadline before and after each chunk.

This mirrors the existing code-publication byte/time checkpoint approach. It
does not add file validation, metadata probes, persistent state or new I/O.
Individual synchronous storage calls still need their driver's timeout; the
cooperative checkpoint cannot interrupt a blocked read. Code mapping, relocation,
publication and app/provider lifecycle remain independent of this change.

The pinned Arduino 2.0.17 ESP32-S3 qio_opi SDK configuration defines
`CONFIG_FREERTOS_HZ 1000` in `tools/sdk/esp32s3/qio_opi/include/sdkconfig.h`.
Thus one requested delay tick is nominally 1 ms in this target configuration.
Counting delay calls does not establish elapsed hardware latency: wakeups depend
on tick alignment, other tasks and actual flash-read duration.

## Deterministic production-reader tests

`test/run_performance_loader_test.sh` compiles the real reader and validator and
injects deterministic read/RTOS timing. A valid 256 KiB padded ELF always takes
64 read calls. Before this change it always requested 64 one-tick delays.

| Simulated time per read | Delays after change | Largest bytes between yields |
|---|---:|---:|
| 0 ticks | 8 | 32,768 |
| 1 tick | 32 | 8,192 |
| 3 ticks | 64 | 4,096 |

Short immediate reads below the thresholds need no extra forced delay. Slow
reads still yield at the time checkpoint. The tests include timeout from slow
I/O, timeout during a delay, tick-counter wrap, short reads and existing
allocation/structure/relocation/scope/publication failures, both with and without
the optional performance hook. ASan/UBSan runs use the same production code.

## Composition with the byte cache

The [separate immutable-byte cache](APP_IMAGE_CACHE.md) eliminates read work on
hits; these checkpoints reduce unnecessary forced delays on remaining misses.
For the existing 301,772-byte X4 default and 185,748-byte springboard ELF builds,
the real production reader was run before and after this change, using five
launches (default/app/default/app/default) and zero simulated read ticks:

| Configuration | Read calls | File bytes | Read-delay calls |
|---|---:|---:|---:|
| Original uncached reader | 314 | 1,276,812 | 314 |
| Cache only | 120 | 487,520 | 120 |
| Checkpoints only | 314 | 1,276,812 | 37 |
| Cache and checkpoints | 120 | 487,520 | 14 |

All rows freshly relocate and publish five mappings. No physical timing or
hardware qualification is inferred. `test/run_image_byte_cache_test.sh` accepts
`ELF_BENCHMARK_DEFAULT`/`ELF_BENCHMARK_CHILD` for real-image operation counts and
an optional `ELF_READER_SOURCE` for comparison with a saved production reader
from another source checkpoint.
