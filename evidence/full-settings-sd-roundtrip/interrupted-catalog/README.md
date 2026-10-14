# Full composition after interrupted-catalog admission changes

Both optimized and ASan/UBSan complete-composition runs passed after integrating
`aed2fe0` and `7cedb73`, equivalent to integration Runtime `0b1d5c4`.
Before both runs, the isolated worktree was verified byte-for-byte equivalent
under `src`, `sdk`, `experimental`, and `test` using:

```
git diff --exit-code 0b1d5c4 -- src sdk experimental test
```

Local prerequisite equivalents were `cb1a862` and `f7665fc`. The test source
remains unchanged from the qualified durable composition at `3a1723e`.

Each run executes both complete campaigns:
- Normal actual Settings/Wi-Fi saves through durable checkpoint, full simulated
  native erasure, typed checked SD restore, and fresh actual controller/readers.
- Real SD publication followed by cancellation before native acknowledgement,
  fresh native objects, exact durable-marker `AcknowledgePrefix` recovery,
  full simulated native erasure, typed restore, and fresh controller/readers.

Both namespace 1 and 6 restorations preserve the entire shared SD sector image
byte-for-byte. Both campaign hashes are
`f003bb59eea72f0fc7325c581984c5c333909a2b422d7ef4b8c36b8da4f68f6c`.

The exact separate System closures remain pinned at Settings `5636756` and
Wi-Fi `a5bd60f`; the runner checks them and all recorded external inputs again
at completion. Full source manifests, build commands, sanitizer environments,
erased-artifact hashes and binary hashes accompany these receipts.

ASan/UBSan pass with leak detection disabled. The previously documented host
LeakSanitizer inspection limitation remains; no hardware, target firmware,
actual Files app, update delivery or product-policy qualification is claimed.
