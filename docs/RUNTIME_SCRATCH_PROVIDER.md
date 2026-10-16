# Runtime 0.4.4 scratch provider increment

The existing `file.open.source@1` service still owns the single staged source,
its immutable sealed view, selected receiver, token revocation, cancellation,
and unload/handoff cleanup. Its application and `FileSourceBackend` tables are
unchanged. No application changes or new storage selection are made here.

## Boundary and namespace

`FileSourceFiles` consumes only an explicitly supplied `RuntimeFiles` session:
open, write, info, seek, read, checked close, and remove. It has no POSIX include,
descriptor, native open flag, or storage fallback. Missing/unavailable providers
fail closed. The selected logical name is exactly `/.risc-file-source`; callers
cannot select a physical root, arbitrary path, or another session. No full-image
allocation or borrowed application buffer is introduced.

The default-off native assembly registers a private `NativeScratchFiles`
provider for the existing `/appdata/.risc-file-source` backing. That concrete
provider owns exclusive creation, regular-file checks, no-follow behavior,
positioned operations, checked close, errno/false-success defenses and native
I/O fault classification. It uses the unchanged shared `storage.files@1` table;
unsupported methods return unavailable (retained after terminal provider loss).
No stage, seal, handoff or application policy is implemented in the provider.
The provider, its generic session and semantic service live through reset.

The native radio lease still encloses the real AppData mount guard. Every
successful operation closes its provider token before releasing those guards.
Failed close or terminal I/O pins the provider/session/mount/radio custody and
admits no retry, cleanup or release. The original 512-byte native transfer bound,
64-KiB reserve and backing-store maximum size are explicitly selected here;
they are not global generic-file limits. Storage remains optional and the
feature-off translation unit contains no symbols.

## Integrity and error compatibility

A staged/sealed length mismatch or unexpected short/zero sealed read contradicts
the semantic service's recorded immutable source. The service now retains its
generic session immediately, before close or remove, for every backing provider.
This preserves the native terminal integrity behavior without placing hardware
flags in the generic consumer. Failed reads/close publish no bytes. Legal short
writes remain supported. Other provider-reported recoverable I/O follows checked
close and the existing Runtime cancellation contract.

`RISC_FILES_NO_SPACE=-9` is an additive generic result for known-custody capacity
or quota exhaustion. Prior numeric results, flags, table layouts and the
`storage.files.directory@1` capacity result (-8) are unchanged. The generic
adapter accepts NO_SPACE; unknown statuses still become IO; any failed close
still retains. Older clients can conservatively map the new result to IO.

## Qualification

- Pure in-memory generic provider: private name, absent/unavailable provider,
  capacity exhaustion, unknown result, changed staged/sealed length, short read,
  invalid transfer count, failed close, lost safety and terminal no-retry. Normal
  and ASan/UBSan pass. The test has no undefined POSIX file-operation symbols.
- Real provider streaming: 12 MiB + 123 bytes with bounded buffers, random read-at,
  full store/partial write, reservation, cancellation, cold exact-name cleanup,
  original-data preservation, symlink refusal, false-success open and failures.
  Normal and ASan/UBSan pass.
- Actual Runtime: all 28 admission, caller/receiver, resident/legacy handoff,
  load/init failure, stale token, full-store, Home, cancellation and retained
  cleanup scenarios pass normal and ASan/UBSan.
- Real pinned LittleFS mount: healthy, open/read/close failure, short-read,
  changed-size and remove-fault all pass normal and ASan/UBSan. The native guard
  and radio lease stay retained and later provider calls do not occur on faults.
- Generic adapter and C compatibility: 5,394 checks normal/sanitized, including
  NO_SPACE through every generic operation, unknown-result fail-closed checks,
  retained closes and unchanged historical status values.
- Current native source, feature-off source and Runtime/API layout compile with
  official ESP-IDF 6.1 `fff9895c82d744c7237be8847347bdd1b07c6643` and Xtensa GCC
  15.2.0. The runner rebases Runtime source and header paths to this checkout,
  preserving only configured SDK/generated inputs. The feature-off object has
  no symbols. The target read-at frame is 4,160 bytes, including the existing
  4,096-byte private output buffer. No image-sized buffer or heap allocation is
  added by this service/provider. Native static state is 72 bytes for the
  service, 168 bytes for its generic session and 108 bytes for the provider.
  Existing PSRAM loader policy is untouched.

The target manifest in `evidence/runtime-scratch-044/target-compile.json` records
exact commands, source hashes and object hashes. The workflow runs host and real
mount qualification independently. These are source/software checks, not a full
firmware link or hardware qualification. Integration and product publication
remain separate steps.
