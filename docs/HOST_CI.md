# Host CI execution and compile reuse

`integration.yml` owns the full integration suite for every pull-request base,
manual dispatch, and reusable-workflow caller. `shared-settings.yml` retains its
focused settings tests, including the distinct UBSan-only configuration, but no
longer launches a second full integration workflow for the same PR.

## Coverage and failure reporting

`test/host_suite.json` contains the **unchanged 184 commands** from the former
monolithic host step at source `9f8d67029181f6cd064e247aa949a295854fc494`.
The reference step took **42m12s** on ubuntu-24.04:
<https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/38939744381/job/117104092406>.
This is measured elapsed step time, not measured compiler-only time.

The commands run in four independent jobs:

- `normal`: 90 normal/variant commands, plus the existing disk-image/LittleFS
  proof and external-clock integration. The initial app-data artifact is produced
  exactly once here.
- `asan-1` / `asan-2`: 41 original ASan/UBSan commands each. Their original
  environment, flags, fixture selection and sanitizers remain unchanged.
- `tools`: the 10 Python commands and 2 TSan commands, plus the new CI correctness
  checks. NVS and Bluetooth dependencies remain pinned to their original sources.

The runner executes every assigned command even after another command fails,
records its exit code and elapsed seconds, and returns failure if any failed.
`fail-fast: false` prevents one failing shard from cancelling the others. Each
command has a log group, a summary row and a downloadable JSON timing record.
The original **Host integration and version guards** check is an aggregate that
requires every shard to succeed, including rejecting skipped/cancelled results.
The existing target job still waits for that aggregate, retains all firmware,
real-ELF, layout and candidate gates, and builds targets with `-j 1`.

To add/change tests, edit the command manifest and intentionally update the
baseline coverage assertion in `test/host_ci_test.py`. The assertion records the
original command digest, sanitizer counts and shard distribution so migration
cannot silently drop or duplicate a test configuration. Sharding uses independent
processes/filesystems; it does not run the old scripts concurrently in one tree.

## Cache correctness

Most existing scripts compile and link several source files in one command.
Simply prefixing those calls with ccache would not cache the repeated Runtime
translation unit. `scripts/host_compiler_cache.py` installs **job-local** wrappers
for host C++ compiler names. For supported compile-and-link commands containing
exactly one `src/bootstrap/Runtime.cpp`, it compiles that translation unit through
ccache and substitutes its object at the same position in the original command.
All remaining test/fixture sources, linker arguments and test execution stay as
before. Unsupported commands/options bypass the wrapper unchanged.

ccache validates preprocessed source and included-header content, compiler
content and compilation options, including sanitizer/feature flags. Direct mode
and all sloppiness are disabled; a new higher-priority header cannot reuse an old
object. Cache storage is bounded to 1 GiB per job, with independent writer keys
for each shard/settings/target job. Restored caches are merely candidates:
ccache still validates each actual compilation. Compiler diagnostics are replayed
on hits. No test result, executable, fixture directory, target compiler, firmware
input, release pin, or hardware-validation claim is cached or changed.

`test/host_ci_test.py` checks real cache reuse across fresh temporary output
paths; source, included-header, compiler-content, flag and sanitizer invalidation;
unsupported-argument fallback; compile failure propagation; exact command
coverage; and continued execution plus failure reporting. Run it with ccache and
g++ available:

```sh
python -m unittest discover -s test -p host_ci_test.py -v
```

For a local cached suite, install the wrapper into a new temporary directory,
then put that directory on PATH. Keep the ccache safety settings used in CI:

```sh
export CCACHE_COMPILERCHECK=content CCACHE_NODIRECT=true CCACHE_SLOPPINESS=
wrappers="$(mktemp -d)"
python scripts/host_compiler_cache.py --install "$wrappers"
PATH="$wrappers:$PATH" python scripts/run_host_suite.py --shard normal --output build/host-normal.json
ccache --show-stats --verbose
```

Set `RISC_HOST_CACHE_DISABLE=1` to run the exact original compiler commands for a
paired same-machine comparison. Compare uncached, cold and warm runs separately;
do not present a warm-cache measurement as a cold first-run guarantee. CI job
summaries and timing artifacts make subsequent comparisons observable.
