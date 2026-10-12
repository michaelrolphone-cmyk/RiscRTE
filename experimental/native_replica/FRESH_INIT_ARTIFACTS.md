# Checked FreshEmpty artifacts (experimental, read-only)

`CheckedFreshInitArtifacts.h` inspects only a bound firmware-private root's
`n%08x/{replica-a.bin,replica-b.bin,.pending}`. It creates, repairs, removes and
publishes nothing. It does not inspect NVS or competing legacy sources; the
receipt/sequence and complete source-inventory validators must do that separately.

The supplied image must equal the internally derived, namespace-bound 484-byte
RTENR03 revision-1 FreshEmpty image byte for byte, including both image and
provenance CRCs, zero identity/incarnation/export marker and empty map/groups.
The caller cannot choose a different expected image. A bank must be exactly 508
bytes; scratch may be any 0..508-byte exact RTENOBJ prefix. Bank B and unknown
names refuse. Any artifact requires a positive persisted sequence no greater than
UINT64_MAX/2. ID bytes must admit an even ID in [2, next << 1]. Partial low bytes
are checked by their smallest possible completion, with no range enumeration or
shift by 64. No ID is allocated here.

## Custody and filesystem boundary

Hooks observe Core status and require the native worker's exclusive serialization
lease. Cooperate false cancels with Busy after checked cleanup; cancellation
arriving after the last cooperation point can complete normally. Retained is sticky and
outranks ordinary errors, cancellation and lease loss. Losing worker/lease with
any open file/directory retains custody and stops I/O. Failed close/closedir is
never retried. Destruction does not close retained handles. Callers must retain
the enclosing owner after `exitSafe()==false`.

Host inspection rejects symlinks in root components/namespace/files, hard-linked
files, non-regular files, unknown/duplicate entries and directory read errors.
It uses no-follow opens, anchored namespace/file access, checked EOF, independent
path/descriptor identity and size/timestamp/link comparisons. Fixed-name lookups
cross-check the completed directory enumeration. A missing namespace is looked up
again after cooperation and fenced against both open-root and root-path changes. Collection is bounded to five
entries and a single 508-byte object buffer, plus fixed metadata/path locals.
No heap allocation occurs inside the inspector. The lease must exclude every
writer, rename and cleanup owner throughout, including provisioned root ancestry;
this is not protection against privileged mount changes or arbitrary dishonest
filesystem/syscall implementations.

ESP_PLATFORM and other non-Linux targets deliberately return Io before filesystem
I/O. There is no fallback to stat or an invented no-follow flag. Pinned ESP
LittleFS stat/fstat metadata has not qualified generic POSIX identity/link checks;
host results and cross-compilation cannot authorize target activation. Production
AppData, SDK, driver and product binding are unchanged.

## Focused evidence

Base source: published `d3f0e7b8450d7f0e5b59c6bbe4cd34ee89e5edbd` (the reviewed
recovery designs on top of `e471163431b0ad2d324c81db4b55c13957c93699`).
Run `test/run_native_fresh_init_artifacts_test.sh` with a C++17 Linux compiler.

Normal strict compilation (`-Wall -Wextra -Werror`) and execution passed:

- 3,054 exact stage prefixes: every length 0..508 for six IDs, including high-byte
  carries and UINT64_MAX-1; zero sequence refuses every stage length
- 125,314 non-ID prefix-byte corruptions; every truncated bank and bank byte
- Partial zero low-ID completions at all seven partial byte widths; odd/zero/
  above-counter IDs; maximum valid sequence and overflow framing
- Noncanonical caller images even with repaired provenance/image CRCs; wrong
  files, oversize, symlinks/hardlinks, subdirectories, FIFO and invalid roots
- 1,626 filesystem/callback fault cases: before/after checked reads/stat/close,
  open and directory failures, incomplete enumeration/short reads, custody loss,
  observed Retained and ordinary I/O status, cancellation, and callback reentry
- Same-size canonical bank/scratch replacement during cooperate is refused;
  namespace creation or root replacement after a missing lookup is also refused
- Retained reentry performs no further I/O or callbacks and never reports exitSafe
- Separately compiled ESP_PLATFORM mode returns Io with zero filesystem calls;
  observed Retained still wins

The shim models open/fdopendir errors before acquisition, not impossible hidden
successful acquisitions returned as failure. Close errors are tested both before
and after real descriptor closure; the inspector never retries either. Test-only
cleanup releases retained descriptors after the owner assertions. Ordinary I/O
status faults are injected at status-observation callbacks; reentry and lease/
Retained transitions cover every callback boundary. Fault counts are from the
runner's `/tmp` fixture path and can vary with root ancestor depth.

Final qualification commands:

```
test/run_native_fresh_init_artifacts_test.sh
SANITIZE=1 SANITIZERS=undefined test/run_native_fresh_init_artifacts_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address test/run_native_fresh_init_artifacts_test.sh
XTENSA_CXX=/path/to/xtensa-esp32s3-elf-g++ test/run_native_fresh_init_artifacts_target_compile.sh
```

Normal, UBSan and ASan runs passed the same focused suite and separate
ESP_PLATFORM refusal executable. LeakSanitizer cannot run under this executor's
ptrace environment (its initial default run reported that limitation); the ASan
rerun disables leak detection. This is not a LeakSanitizer pass.

The pinned Xtensa compiler is `crosstool-NG esp-2021r2-patch5`, GCC 8.4.0, Linux
compiler binary SHA-256
`5b30f4eb7c41b7954acd4f183895f03abdb612d8470346436d850208b8be2c98`.
Strict C++17 `-Os -mlongcalls -fno-exceptions -fno-rtti` compilation passed both
normal and `-DESP_PLATFORM` modes. Their object bytes match; undefined imports
contain only `strlen`, with no filesystem functions. The target class is 184
bytes; its emitted inspect frame is 80 bytes, check/expectedAt/expectedCrc frames
are 32 bytes each, and the test entry frame is 48 bytes. Those are individual
compiler frames, not an aggregate stack bound including hooks. The target code
remains the unsupported, fail-closed implementation.

For comparison, Linux x86-64 GCC `-O2 -fstack-usage` reports a 224-byte class,
1,216-byte inventory frame, 1,072-byte file-inspection frame and 80-byte inspect
frame. These are separate, potentially nested frames, not a claim that the
508-byte logical content buffer is the complete stack cost. No allocator or
heap-allocation hook is added.

These tests are isolated native inspection evidence. Full initializer/store
power-cut integration, NVS durability, target filesystem qualification and
production activation are separate obligations.
