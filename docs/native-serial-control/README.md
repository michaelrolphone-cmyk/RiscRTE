# Serial control progress during native storage work

Runtime 4b3998f8091d1948a702a281757a1935d401ea4a correctly fences storage dispatch while native settings owns storage. Its fence also defers RAM-only HELLO and NS requests. The browser installer uses a 5000 ms request deadline and treats expiry as an uncertain terminal connection.

The owner-context control checkpoint services validated HELLO, NS and PATH without storage callbacks. PATH is accepted only without a live handle. Other requests remain pending until storage is safe. At 4000 ms they receive explicit `RISC_FILES_BUSY` without dispatching storage. A BUSY reply is a failed install attempt, not successful transfer. The client must stop; this does not authorize automatic retries.

Normal service checkpoints remain the only storage dispatch route. Live-handle cleanup waits for full provider safety. An idle disconnect discards protocol RAM without manufacturing a provider operation. Existing sequence/replay validation remains in the real command parser.

## Bounded diagnostic capture

The first deferred request and first admission deadline in each connection record three events in the existing 16-event retained diagnostic ring. No extra retained ring is allocated. The observer makes no provider calls, allocation, blocking wait or live USB write. Startup text chatter uses a different ring and cannot evict these events. Later native events can still evict older events at the fixed capacity.

Read the existing `diag` replay after the protocol connection is stopped:

- `serial-first-identity` / `serial-deadline-identity`: detail is the session's 32-bit pattern, cause is the request sequence.
- `serial-first-state` / `serial-deadline-state`: detail identifies HELLO=1, NS=2, PATH=3, STAT=4, GET=5, PUT=6, READ=7, WRITE=8, COMMIT=9, CLOSE=10, ABORT=11, MKDIR=12; zero means unknown.
- State cause bits: 0 active, 1 entered, 2 discovery, 3 hydrated, 4 attached, 5 retained, 6 native storage pin, 7 native lifecycle exit-safe.
- `serial-first-wait` / `serial-deadline-wait`: detail is elapsed admission wait in milliseconds.

The snapshot does not expose the provider's private async-session identifier. It cannot infer missing transitions or explain an earlier lost event. Diagnostic capture does not change storage admission.

## Qualification

Run `bash test/run_serial_files_test.sh`, `bash test/run_diagnostic_checkpoint_test.sh`, and `bash test/run_native_serial_files_transport_test.sh` with the pinned IDF source in `IDF_PATH`. Repeat with `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` for ASan/UBSan. Leak checking is excluded in this host environment.

The product scheduler composition separately exercises full HELLO/NS/PATH/STAT and guarded replacement/create/readback through actual native settings, Runtime, SD and FatFs code. Its scheduling/media/NVS stand-ins are not hardware validation. Preserve the .58 hardware NS-timeout episode independently of these software results.
