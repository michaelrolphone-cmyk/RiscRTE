# Full controller / durable SD / native-erasure qualification

This final composition is based on Runtime `a585029c2dc18da1e3dab34398420a69e450b681`,
with the exact production follow-ons `2b10a8c` (bounded checked restoration
catalog) and `8a32eca` (durable normal-SD marker recovery). Base `965d032` is
patch-identical to prerequisite `8aaf3e9`.

The test-only baseline was preserved at `3d99159`; its normal and ASan/UBSan
qualification was preserved at `6b2f02d`. This directory records the final
compatible durable composition, not an untested combination of those reports.

Both optimized and ASan/UBSan runs execute:

- Normal actual Settings and Wi-Fi saves, durable SD checkpoint, complete
  simulated native files/NVS erasure, typed checked SD restoration, and fresh
  actual controllers/read-through readers for namespaces 1 and 6.
- The same complete path with cancellation after actual SD publication and
  before native acknowledgement, followed by new native objects, checked load
  of the persistent export marker, and required `AcknowledgePrefix` recovery.
- Exact native map equality, both full synthetic Wi-Fi credentials, unrelated
  file open/read/close, live-custody rejection of Files-style access and USB,
  post-release USB admission/read/end, and full SD-image hash preservation
  through typed restoration.

The exact System dependency closures remain separate: 75 Settings compiler
inputs at `5636756db893d682ad42a595625b226f40bbba4d` and 63 Wi-Fi compiler
inputs at `a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89`.

ASan/UBSan use `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. LeakSanitizer's process-inspection failure in
this host is preserved in `../baseline/lsan-environment-blocker.txt`; leak
checking remains unqualified.

See `test/NATIVE_SETTINGS_SD_ROUNDTRIP.md` for command, seams and limits.
No target, hardware, actual Files app, update delivery or product policy is
qualified. The combined interrupted boundary is a clean cancellation and
native-object restart, not process-death power-cut coverage.
