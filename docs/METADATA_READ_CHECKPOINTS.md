# Bounded boot metadata read checkpoints

Runtime 0.2.23 `readJson()` keeps its 512-byte maximum read request, 64 KiB input limit, strict
JSON guard/parser, five-second total read deadline, and single-close/retained-close
handling. All selected board, provider and application metadata is still admitted
before driver execution. No metadata cache or admission shortcut is introduced.

On ESP, the reader now requests a scheduler tick after 32 KiB or two milliseconds
of elapsed read work, whichever comes first. Remaining bytes receive a final
checkpoint at EOF (or the size bound), so a sequence of individually small
manifests still cooperates once per file. The total deadline is checked before
each read and after any checkpoint; time spent waiting still counts. A synchronous
I/O operation needs its driver's existing timeout because a checkpoint cannot
interrupt it. Parse and graph-validation work remain unchanged.

## Deterministic production-path evidence

`bash test/run_metadata_checkpoint_test.sh` compiles the production reader and
parser with substituted stdio, steady-clock and RTOS boundaries. Both disabled
and enabled performance tracing are exercised. `SANITIZE=1` enables ASan/UBSan.

It covers short files; nonzero short reads; EOF; exact 32 KiB/64 KiB boundaries;
byte/time scheduler fairness; sequential small files; deadlines before reads,
during reads and during scheduler waits; the existing strict five-second
comparison; malformed/duplicate-key/oversize input; read/open/allocation errors;
single-close failures; and sticky retained ownership. Existing
`run_metadata_read_test.sh` covers full Runtime preparation and metadata-failure
admission/lifecycle barriers.

For a successful 64 KiB input, zero simulated read time now requests two ticks
instead of 129. A 32 KiB file with one millisecond simulated per read yields after
every two reads; with three milliseconds per read it yields after every read.
These are operation counts under injected timing, not hardware measurements.

Optional real-store mode reads the exact boot, board, selected driver rows,
application policy manifests, and retained cohort metadata without modifying the
store or executing ELFs:

```sh
bash test/run_metadata_checkpoint_test.sh --store /path/to/store
```

For the unchanged Watch public-source store used for this investigation,
the sequence contains 58 file reads (the I2C manifest is read once for each of its
two selected instances), 101,099 bytes and 283 `fread` calls. The old production
reader requested 283 one-tick waits; the new reader requests 59 under zero
simulated I/O time, with at most 32,768 bytes between checkpoints. Slow storage
can require more time-triggered yields. No timing reduction in milliseconds is
claimed for the physical Watch.

## Measuring the actual wake

Existing stage logs separate bank selection, store mount, app-data preparation,
provisioning, metadata preparation, default ELF loading, application init/entry,
and provider load/start. Enable `RISC_STAGE_LOGS=1` on the exact product-compatible
profile to capture those boundaries. `RISC_PERFORMANCE_TRACE=1` additionally
records metadata reads/parsing and aggregates metadata wait calls/ticks/time as
phase 33. Both options are otherwise disabled by ordinary profiles.

Compare the same image/store, wake cause and persisted product settings. Stage
and performance intervals are inclusive and must not be summed when nested.
Setup timestamps omit ROM/bootloader work. App entry and present submission are
not proof of visible pixels; a complete crown-to-visible-clock measurement needs
an external edge-to-display observation. Hardware access and qualification are
separate from these host tests.
