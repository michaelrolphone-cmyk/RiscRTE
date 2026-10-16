# Provider-backed ELF loading

Runtime selects explicit `storage.files` sessions. Logical image names are never
reopened through a process filesystem, and missing optional storage fails the
requested load without implicit source fallback. An explicitly configured
`internal_fallback` remains supported through the same generic internal reader.

## Consumer inventory

- `bootstrap/Runtime.cpp`: default and ordinary applications, loose children,
  internal drivers, internal fallback, and policy-pinned provider admission.
  `readInternalImage` validates and runs managed-image admission after a checked
  close, then transfers the one raw allocation to the loader.
- `bootstrap/RemovableAppsRuntime.inc`: configured external applications and
  drivers acquire the exact configured provider/capability/instance. They read
  through `RemovableAppImage` and `RuntimeFileImage`, retaining the graph grant
  until close is known safe. Internal fallback uses the same internal reader.
- `runtime/drivers/ProviderModuleV2.cpp`: ordinary Runtime specifications carry
  an explicit image callback; pinned modules retain their immutable graph image
  and existing verification/relocation path. Standalone legacy graph callers may
  still use the library's legacy path APIs; Runtime never selects that path.
- `ports/esp32s3/NativeBankStore.cpp`: provisioned-image inspection and staged
  candidate admission use the candidate's exact source view. Provisioned reads
  share the original aggregate admission deadline.
- `lib/elf_loader/src/dlso/dlmod.c`: cached-reader misses and pressure retries
  call the supplied reader. Cache hits precede the reader and perform no source
  probe. No reader failure can switch to `esp_elf_open`.

`NativeBankStore::finishApp` is concrete bank-update backend write/read-back/hash
verification, not a Runtime loading escape. Its internal filesystem operations
remain backend-owned. Board-profile bootstrap is allowed to assemble concrete
file providers. Existing non-default application data, installation/provisioning
formats and their concrete provider operations remain compatible and are not
migrated by this change.

## Source selection and compatibility

Existing `image_source` records retain their legacy `storage.volume` meaning.
The optional `capability` field explicitly selects `storage.files` or
`storage.volume`. The existing `volume_instance` field is retained for format
compatibility and identifies the selected capability instance, including zero
for a software-only generic provider. Duplicate eligible providers reject
preparation before activation. Provider identity and instance are copied into
the graph and the exact source dependency is pinned.

A software-only generic source needs no `hardware.device` requirement or legacy
volume table. Legacy minimal read-only volumes are adapted by the concrete
`runtime/storage/VolumeImageFiles` provider. It contains refresh/readiness and
volume-handle semantics; the consumer sees only generic open/info/read/close.
No filesystem/metadata extension is required. Missing adapters do not enable
POSIX fallback. Callback chains check custody after every provider dispatch.

## Ownership, memory and scheduling

`RuntimeFileImage` fills one caller allocation directly, accepts legal short
reads, verifies length/position/EOF, and closes once. Failed/uncertain cleanup or
post-dispatch owner loss retains the source session, token, Runtime candidate and
provider dependencies. It returns a non-retryable error even if allocation also
failed. Terminal source-map custody fences invocation and graph cleanup even
when no ELF handle exists. Retained sessions are never redispatched.

- Ordinary/removable loading: one full raw image plus relocated sections.
- Cache: raw allocation ownership transfers without a full-image copy; four
  entries and 1 MiB total; disabled without PSRAM. Temporary provider absence
  does not invalidate admitted cached bytes. Terminal custody still blocks use.
- Pinned admission: input plus graph copy, then input freed. Activation keeps the
  graph bytes plus one fresh verification/relocation buffer, never a third copy.
- Ordinary allocation keeps `esp_elf_malloc` PSRAM reclaim/retry behavior and its
  no-internal-fallback policy. Pinned admission preserves PSRAM-first legacy
  fallback. Native-bank admission remains PSRAM-only.
- Reader checkpoints between synchronous provider calls yield after 32 KiB or
  2 ms and enforce a 30-second deadline. They cannot preempt a blocking provider
  callback; bounded per-call work/time remains the provider's responsibility.
  Native yields use `vTaskDelay(1)` directly, including candidates without a
  cooperative port delay hook. Validation/admission retains its existing checks.
- Cold internal reader instrumentation preserves phases 40/41/45/46/44; cache
  hits omit source phases. Failure tracing cannot alter retry classification.

## Qualification

Focused workflow: `.github/workflows/runtime-elf-provider.yml`.

- `run_runtime_file_image_test.sh`: in-memory source bytes with no filesystem
  counterpart; legal short reads, exact active/staged selection, absence,
  bounds/mismatch, mutation/truncation, allocation, close and custody faults.
- `run_reader_image_cache_test.sh`: production reader cache/validator/relocator,
  same-reader reclaim retry, no fallback, PSRAM on/off and zero-copy ownership.
- `run_removable_app_image_test.sh`: minimal legacy volumes and safety loss
  between every chained callback, including read before error inspection.
- `run_removable_runtime_test.sh`: actual software-only generic provider at
  instance zero, legacy sources, application/driver/fallback loads and retained
  custody; ambiguous sources reject before activation.
- Native registry/cache/pressure tests use provider-supplied bytes and reject
  legacy direct opens. Cache tests distinguish temporary unavailability from a
  retained sibling source map.
- `run_runtime_image_custody_test.sh`: actual Runtime and graph dependency
  retention on read/close/allocation faults, no fallback or second-run I/O,
  deterministic 32 KiB/2 ms fairness and 30-second read/yield deadlines, and
  trace ordering/error preservation. Only intentional-retention subprocesses
  suppress leak reporting; clean and recoverable CI cases retain leak checks.
- Native-bank admission tests inject faults through generic file callbacks.
- `compile_runtime_metadata_target.py --elf` compiles actual Runtime, bank,
  provider graph/module and loader units using pinned SDK6.1 commands, with
  optional storage on/off and embedded assembly. This is compile evidence,
  not a firmware link or hardware qualification.
