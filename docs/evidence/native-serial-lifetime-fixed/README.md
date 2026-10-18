# Native serial lifetime: fixed-source host evidence

The final `wave-*.json` and `product-*.json` manifests are the authoritative
passing runs. Each checks all recorded local sources before/after execution.
They were rechecked against the working source after all four runs finished.
`receipt.json` indexes raw logs, manifests and their SHA-256 hashes.

## Scope and result

- Exact released Waveshare `sdmmc-fatfs` 0.2.2 source at
  `e1b2fc8b801b33e3bd6ea30626a8dda9bf2237a6`, unchanged provider/FatFs code.
- Actual Runtime graph/native startup binding, clean Runtime return, and the
  `Runtime::nativeFilesCheckpoint()` method called by the actual `main.cpp` loop.
- Independent private namespace 2 through actual native AppData mount fencing,
  `ReopenedFiles`, ESP LittleFS 1.14.1 / LittleFS 2.9 pinned source and qualified
  IDF6 mtime32 port. Namespace 12 uses the SD provider. They are never aliases.
- Eleven C++ cases pass normally and with ASan+UBSan: cold, absent-card, healthy,
  disconnect, failed-close, failed-write, failed-release, failed-prepare,
  post-return, post-failed-close, default-run.
- After return: old SID rejected; fresh HELLO/all NS succeed; old PATH absent;
  new PATH/STAT, create, create-existing guard, replace-missing guard, explicit
  replacement, COMMIT, byte/CRC readback and CLOSE pass. Same logical pathname
  stores different bytes in namespaces 2 and 12. Absent/retired SD remains
  UNAVAILABLE while independent namespace 2 remains usable.
- Sixteen fresh sessions retain one provider start, no premature quiesce/stop,
  and no physical LittleFS handles. Eight repeat-run attempts are rejected
  before and after final stop. Successful final stop unloads exactly once;
  repeat stops are idempotent. Failed close/release stays pinned and fenced.
- The unchanged actual `scripts/serial_files.py` Client completes 103 requests
  over host pipes with its default 5-second deadline, in both configurations.
  Full TX/RX, including CRC-bearing CLOSE, is retained in the raw logs.
- Synthetic product tests pass thirteen cases for each `legacyWave12` and
  `X4SD9` selector, normally and with ASan+UBSan.

The host static workspace symbols are 2976 bytes for the protocol, 864 bytes
for the private provider, and 488 bytes for namespace sessions. The repeat tests
check lifecycle/handle bounds; these numbers are not a target heap measurement
or a proof of absence of every possible leak.

## Failed attempts and corrections

1. `attempt-incomplete-read-close.log` is a test error, not a Runtime failure.
   The exact test source and input hashes are preserved alongside it. It issued
   GET then immediately CLOSE, with no READ. `Files.cpp` only admits CLOSE when
   `offset_ == size_`; the requested provider close and injected unlock failure
   were never reached. The assertion wrongly expected terminal retained custody.
   The corrected post-return case uses PUT replace, WRITE, injected
   `fail_unlock_`, then ABORT, which closes the owned write handle through the
   actual provider. It returns retained custody and fences further dispatch.
   Successful read transfers separately read all bytes then verify CLOSE CRC.
2. Initial ASan+UBSan runs failed because LeakSanitizer cannot operate under
   this executor's ptrace environment. Raw errors are preserved. Final runs use
   `ASAN_OPTIONS=detect_leaks=0`; address/undefined sanitizers remain active.
   No LeakSanitizer success is claimed.
3. The first host-only redirect header renamed POSIX declarations before the
   C++ standard headers had imported them. That failed compilation. The final
   helper includes the real system headers first, declares exact-typed private
   redirects, then redirects only `ReopenedFiles.cpp` calls. Runtime metadata
   and ordinary host file I/O remain libc-backed. The failed generated header
   and compiler log are preserved.
4. `superseded-wave-*` passed against the first fixed Runtime source. They are
   retained for audit but superseded by the final manifests, which include the
   additional pre-parking old-session/ownership safety checks.

## Limits

This is source-level host qualification with host ELF loading, RAM SD media,
RAM flash, and GPIO/sync/storage-worker/CPU/VFS boundary shims. The real native
worker task, actual CpuPort, target final linking, USB serial, physical SD,
and physical hardware are not qualified. No device was opened, reset or flashed.
These results do not identify the physical cause of the earlier `.59` trace.

The X4 actual-provider fixture was updated for final-stop expectations but was
not run: its pinned X4/StorageFatFs source inputs are unavailable locally. The
synthetic X4SD9 mapping test is explicitly distinct from that qualification.
