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

## Cumulative provisioning extension

The original 184-command prefix and its regression digest remain unchanged. T023 adds six new normal/ASan-UBSan pairs: request queue, compaction, native request bridge (both SDK signatures), preserving policy, ordinary-boot gate and NVS radio quarantine. The 196-command suite runs each command once across the existing four shards; no extra duplicate workflow is added. Runtime/HTTP/removable/provisioning tests already in the baseline exercise their expanded cases in place.

## Measured first CI result

On the unchanged Runtime source tree, the old 184-command step in run
[38939744381](https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/38939744381/job/117104092406)
took **42m12s**. The same commands all passed in
[38949714890](https://github.com/michaelrolphone-cmyk/RiscRTE/actions/runs/38949714890):
normal **9m37s**, ASan/UBSan halves **5m56s** and **6m37s**, tools **1m16s**.
That is **23m26s** summed test-step time (44% less), with a **9m37s** longest
shard (77% less). Runner scheduling and setup made the complete host gate
12m12s. These are observed runs, not a fixed future duration guarantee.

The former Arduino target was a **separate 13m20s job** in the baseline. That
number includes firmware compilation, target-format host checks and packaging;
it is not part of the 42m12s host measurement and is not pure compiler time.
The hardware-result polling workflow independently reported missing exact-source
controller evidence. Software test results do not qualify physical hardware.

## Supported target-format coverage

The integration target now uses the same immutable ESP-IDF 6.1 image and SDK as
`native-idf6.yml`, with the GCC 15.2 ESP32-S3 compiler alias. It runs independently
of the host shards. There is no PlatformIO/Arduino target build or legacy
candidate publication in integration. `release.yml` still invokes the shared
regressions on main/master pushes and manual dispatch, with read-only access;
it had no downstream target-artifact consumers. Native release publication
remains exclusively in `native-idf61-release.yml`.

Coverage moved out of the obsolete target job:

| Previous target coverage | Supported location |
| --- | --- |
| Frozen Runtime header, nine negative ABI controls | Native fixture job, actual Xtensa compiler |
| Seven real default/child/provider/image-cache ELFs | Native fixture job, GCC 15.2, with explicit little-endian validation |
| Production loader trace/read/yield/failure paths, absent hook | Native fixture job, normal and ASan/UBSan |
| Image byte cache, allocation and ELF validation | Native fixture job, original normal/sanitizer variants |
| SPIFFS 83-file high fill and exact 3/128-file boundaries | Native fixture job, current SDK's pinned SPIFFS, same streaming, retry, interrupt and error tests |
| Dense compact image, read-only admission and capacity | Native fixture job, current pinned `spiffsgen.py`, same normal/sanitizer tests |
| Image reproducibility and real store readback | Repeated native image generation plus real SPIFFS read-only/streaming tests; seed corruption/no-overwrite unit cases retained |
| Full bootloader-backed bank, provisioning/cohort/failure cases | Existing generic `native-idf6.yml` normal and ASan/UBSan native-ABI2 runs |
| Boot path with actual firmware bytes | Generic native qualification now supplies its freshly built `riscrte_native.bin` |
| Native linked IQ bank reservations and target invariants | Existing four-product `native-idf6.yml` qualification |
| Initial app-data image and LittleFS power cuts | Unchanged normal host shard and its sanitizer variant |

The retired outputs are the eight Arduino firmware profiles and their legacy
candidate/maintenance/seed packaging bundles. Their Arduino-only ABI1/layout
build configurations are retired, not represented as newly qualified native
firmware. Legacy publishing remains explicitly disabled. The old mkspiffs
command-line adapter's optional roundtrip is replaced by the current image
producer and production SPIFFS readback checks, rather than installing an
obsolete Arduino tool solely for that adapter. Existing Python legacy package
contract tests remain in the host suite.

The SPIFFS host fixture checks every upstream source blob, SDK configuration
header, Kconfig and image producer. The generic native firmware build compares
its generated SPIFFS configuration to the host fixture, so changed target
geometry cannot silently pass. Unknown bytes or differing configuration fail
closed. `native_ci_test.py` covers this behavior and guards the coverage map.
No runtime source, public ABI, released product pins, hardware workflow or
native publication authority changes as part of this CI migration.

Legacy `SEED_DIRECTORY` installation of the generated Arduino candidate and
`maintenance_plan_test.py` against Arduino maintenance bundles retire with those
packaging outputs. They are not claimed as executed native packaging tests.
Shared seed transaction corruption/no-overwrite/determinism contracts remain
in `provision_seed_test.py`; native bank qualification retains production
provisioning, abort/corruption/admission/close-retention and image-install
failure paths. The replacement fixture job checks deterministic native SPIFFS
bytes and actual production store readback. Native product packaging continues
to use its existing independent source-custody and package qualification.
