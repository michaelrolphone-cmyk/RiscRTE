# Scoped GPIO read lookup

Base: faa8f62936a5889c65cce3f271758d9d95f21d7b. The read path reuses the existing 64-byte write hint table. Every access still validates current owner and complete opaque token; stale or colliding hints trigger the original scan. No SDK layout, token allocation, hardware configuration or physical timing change.

Reproduce with `GPIO_READ_BUILD=/tmp/gpio-read bash test/run_scoped_gpio_read_test.sh` and `SANITIZE=1 bash test/run_scoped_gpio_read_test.sh`. The timing binaries use exact uninstrumented production CpuPort. Separate instrumented copies count only executed predicates in the original read search; their timings are not performance evidence.

Each mode runs seven trials of 65,536 modeled bytes, with 1,572,864 reads and 1,048,576 writes per trial. Payload uses DAT0 and CLK; cold clears hints each 512-byte sector; mixed alternates DAT0/CMD while interleaving CLK reads/writes; collision deliberately gives DAT0/CMD the same hash slot. Full outputs are beside this file.

Across 11,010,048 reads, search comparisons fall from 458,752,000 to 41 for warm payload, to 36,736 for cold sectors; mixed three-pin accesses fall from 462,422,016 to 84. Deliberate collisions correctly fall back, producing 154,140,672 comparisons. Host timing improves in all four measured patterns; these are CPU-boundary measurements, not physical SD throughput predictions. Target hardware remains unmeasured.

The separate actual X4 SD/FatFs synthetic-ROM test reports one MiB as 256 file-read calls, 2,065 sector reads, no sector writes and 43,542,590 scoped GPIO calls. Four MiB uses 1,024 file reads and 8,257 sectors. It confirms the high call volume independently of this CPU benchmark, without using commercial ROM contents.

Normal/ASan/UBSan read and write tests cover full-token generations, hash collisions, foreign owners, stale release/reclaim, held and CPU-retired pads, callback failure and token exhaustion. Existing held-output/native-PWM/touch-scope suites also pass with ASan/UBSan. LeakSanitizer is unavailable under the executor ptrace environment and was disabled explicitly; ordinary sanitizers remain enabled.
