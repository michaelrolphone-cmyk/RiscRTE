# Software verification

Executed in the isolated migration checkout. Single-job ESP32-S3 target builds;
no hardware, serial, flash, releases, merges or other task jobs were touched.

Passed:

- `pio run -e esp32s3 -j 1`: firmware ELF/BIN; ESP32-S3 / Arduino 2.0.17 /
  PlatformIO espressif32 6.13.0. Final size recorded in the PR.
- `python3 scripts/build_apps.py`: real Xtensa ET_DYN heartbeat, handoff, child
  and typed provider fixtures; expected entry points and allowed imports.
- `pio run -e esp32s3 -t buildfs -j 1`: SPIFFS image containing boot.json,
  board.json and the actual heartbeat default.elf; no device write.
- `test/run_elf_test.sh`: inherited ELF validator accepts all four target ELFs
  and rejects mutated/truncated section tables, symbols and relocations; inherited
  allocation ledger tests cover repeated exit, capacity, overflow and failed realloc.
- `test/run_runtime_test.sh`: actual host shared module loading, selected typed
  driver startup/quiescence, `default → child → default → missing child → default`,
  traversal/initial-default failure, incompatibility/invalid board rejection,
  and three exact-format monotonic heartbeat lines from the shipped app source.
- `test/run_provider_graph_v2_test.sh`: dependency/cycle/ambiguity/grant behavior,
  owned metadata, retained dependency tables, failed-start recovery, targeted
  recovery preserving another consumer, failed teardown/retry, and fail-stop
  destruction when quiescence cannot be established.
- `test/run_board_test.sh`: complete materialization of the three exact Garden
  shared-schema examples (read-only external inputs), plus SPI signal/CS conflicts,
  missing pins, duplicate controllers, strict booleans, unknown extensions and
  UTF-8 rejection. No Garden functionality was migrated.
- `git diff --check`.

Limitations / interrupted check:

- An ASan/UBSan graph ownership run did not finish in this macOS environment and
  was interrupted. The complete graph suite subsequently passed without sanitizers.
  Sanitizer success is **not** claimed; `SANITIZE=1` remains available.
- Host dynamic modules on this machine are Mach-O. The Xtensa ELFs were built
  and structurally validated, not executed or hardware-relocated by the host.
- Target SPIFFS mount, PSRAM instruction/cache behavior, real serial output,
  physical chip matching and hardware quiescence remain unrun on actual boards.
- T-Watch extension types, privileged drivers, attached storage and automatic
  multiple instances of the same driver package are deliberately unsupported.

PlatformIO initially attempted to write its shared package lock under the normal
sandbox. The build was rerun using a task-local copied package/toolchain core;
no global package installation or unrelated build cache was changed.
