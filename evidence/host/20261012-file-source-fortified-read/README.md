# FileSource fortified-read test harness repair

## Observed hosted failure

Run [38264028081](https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/38264028081)
tested checkpoint `9c4772461532bd5349b5c392d51533ed212c202a`. Normal job
`114861507369` passed. Sanitized job `114861507808` reached the FileSource storage
test after passing startup entropy, replica core, fresh initialization, import
initialization and scratch allocation. It aborted at the injected read-error
assertion on `test/file_source_storage_test.cpp:55`, exit 134. The selected log
lines are preserved here. No hosted rerun of this repair has been performed.

## Diagnosis and narrow repair

The original harness wraps `read` but not glibc's fortified `__read_chk` entry.
On this host, the unchanged test passes ordinary ASan/UBSan. Adding
`-D_FORTIFY_SOURCE=3` reproduces the same assertion and exit 134; the resulting
binary references `__read_chk`. This is a fault-injection coverage gap: the
error flag is never consumed when the compiler emits the fortified entry.
The exact hosted compiler fortification level was not printed in its logs.

The repair wraps both entry points. A fortified over-bound request is forwarded
to the original `__read_chk`, preserving its check. Safe injected failures use
the same buffer-poisoning helper as ordinary reads. The test now also verifies
that exactly one injected failure was consumed, the caller buffer remained
unchanged, and a following successful read returns the expected bytes.

No production source, workflow, feature selection, or target behavior changes.
No device access, activation, publication, or release was performed.

## Local verification

Compiler: Debian GCC 14.2.0. Run the existing
`bash test/run_file_source_storage_test.sh` with `SANITIZE=0` and `SANITIZE=1`
under each of the following compiler configurations:

- Unmodified host default
- `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2`
- `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3`

All six repaired cases pass. Each consumes exactly one injected failure.
The fortified sanitizer cases each report eight calls through `__read_chk`.
ASan/UBSan use `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`, matching the failed workflow. The test runner
retains its original no-PIE sanitizer flags.

To add the fortification flags without changing the runner, place a temporary
`c++` wrapper first on PATH which executes:

```sh
exec /usr/bin/c++ -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE="${FORTIFY_LEVEL:?}" "$@"
```

Set `FORTIFY_LEVEL=2` or `3` for those cases and set `OUTPUT_DIR` to a separate
absolute build directory per case. The pre-repair reproduction used the same
runner compile flags, plus `-D_FORTIFY_SOURCE=3`, and only the original linker
wrap list (without `--wrap=__read_chk`). The test file at the failed checkpoint
and local repair base had identical SHA-256, recorded in `manifest.json`.

Logs, relevant undefined-symbol listings, exact source hashes and job identity
are preserved alongside this note. This is focused host test evidence, not
hardware qualification or a claim of a green hosted repair run.
