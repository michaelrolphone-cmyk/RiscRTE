# Deterministic host stream fixture timing

The stream-session integration fixture keeps its 1 ms requested open, call and
close deadlines. The production deadline remains strict: elapsed time must be
**greater than** the requested milliseconds times 1000 before the broker fences
the session. No deadline, public ABI, provider policy or cleanup contract changes.

## Clock isolation

The fixture's fake Runtime uptime does not measure provider callbacks.
AppStreamSessions normally uses `std::chrono::steady_clock` on the host and
`esp_timer_get_time` on ESP. A host process descheduled across a short otherwise
successful callback can therefore legitimately exceed its wall-clock deadline.
The fixture must not confuse that scheduler noise with the behavior under test.

Two private hooks exist only when `RISC_STREAM_HOST_TESTING` is defined and
`ESP_PLATFORM` is not. A nullable clock hook defaults to the unchanged steady
clock; a nullable observer records the callback phase, provider result, measured
microseconds and requested deadline. Measurement is taken once, before the
observer runs. Both the fixture and production paths use the same strict deadline
comparison. Neither hook is part of an SDK table or compiled into ESP.

The integration harness installs a monotonic fixture clock by default. Ordinary
callbacks consume zero modeled time. The existing `stream_test_slow()` still
sleeps for 4 ms and advances modeled time by exactly 4000 us. No slow case is
removed or accepted as success. Each slow case must reach its intended callback
exactly once, report provider OK at the unchanged 1 ms deadline, and retain its
original app/provider/dependency custody. Other modeled callbacks must take zero
time, preventing an earlier unrelated retention from satisfying the slow case.

The original 43 eager/demand scenarios plus two lifecycle cases remain (88).
Eighteen additional cases exercise open, copied call and close at 999, 1000 and
1001 us in both activation modes. The first two clean up successfully; only
1001 us retains custody and fences stale client authority. The original mapped
image, queue allocation, cleanup, reentry and stale-invocation assertions remain.

## Diagnostics and reproduction

Callbacks print scenario, activation, clock, phase, provider result, elapsed,
deadline and over-budget status on errors, overruns and boundary controls.
`STREAM_TRACE=1` prints every callback. The provider result is not the final
client result: provider OK may become broker RETAINED after an overrun. A final
unexpected runtime outcome prints expected/actual retention, run result and the
ordered callback/event history before the assertion.

`STREAM_CLOCK=steady` leaves the injected clock unset and runs the original
88-case wall-clock matrix, without synthetic boundary cases. This preserves a
route for investigating actual host behavior rather than claiming clock
isolation proves a historical cause:

```sh
STREAM_CLOCK=steady STREAM_TRACE=1 RISC_APP_POLICY_ROWS=17 \
  bash test/run_app_stream_sessions_test.sh
```

At base `13eca114cb58469cf6004fafc3da84fbd9d29beb`, both unchanged 88-case matrices
passed. The historical rows17/copied-control retained-equality failure was not
reproduced. Its assertion alone does not establish which callback fenced or why;
unexpected open or close fencing is consistent with that later assertion.
Synthetic boundary overruns are deliberate controls, not a reproduced natural
failure or proof of the historical cause.

## Verification

```sh
bash test/run_app_stream_sessions_test.sh
RISC_APP_POLICY_ROWS=17 bash test/run_app_stream_sessions_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_app_stream_sessions_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 RISC_APP_POLICY_ROWS=17 \
  bash test/run_app_stream_sessions_test.sh
bash test/run_app_stream_clock_contract_test.sh
bash test/run_provider_queue_host_test.sh
bash test/run_provider_queue_retained_fence_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_provider_queue_host_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_provider_queue_retained_fence_test.sh
```

The compile contract checks ordinary host, host with testing, ESP, and ESP with
the testing define. Host clock selection remains steady by default. Both ESP
objects are byte-identical and have no timing-hook symbols. Optimized ordinary
host and ESP broker objects were also byte-identical to the base revision.
The ESP check uses the existing declaration-only host shim, not an ESP toolchain
or physical hardware execution.

Local negative controls separately changed strict `>` to `>=` and disabled
deadline rejection. The first was rejected at exactly 1000 us; the second failed
the 4000 us slow-case retention assertion. These mutations are not shipped.

ASan/UBSan runs disable leak detection because retained fixtures intentionally
preserve mappings and custody and exit without destroying them. This is not a
LeakSanitizer pass. Successful paths still assert zero allocated queue bytes.
The focused Stream session timing workflow runs all four fixture matrices,
clock-isolation compilation, and ordinary/sanitized queue custody regressions.
The existing broad integration workflow still runs its original fixture entries.

The repository's source-version gate requires 0.4.15 to advance to 0.4.16 for
any `src/` edit, including these guarded test hooks. This metadata bump is not a
release, deployment, product build, or hardware qualification.
