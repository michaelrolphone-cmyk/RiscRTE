# Bounded native catalog collect-and-next scheduling

Source increment `3dd409e559f2d526883b54f240a84a5fd54cecb6` queues at most one
next directory entry immediately after collecting a successful native directory
result. Previously the submission alone consumed another owner poll and its
scheduler delay. The consumed operation token is cleared and the copied result
is wiped before the next submission. No provider work executes in that step,
and there is no loop through entries. BUSY retries on a later poll. Cancellation
still drains the one accepted operation before checked close and release.

The change is native-only. It changes no object fields, ABI, history window,
inventory/work bound, dataset authority, mutation order, source selection or
product flag. It neither increases retained history nor deletes historical data.

## Measured source-pinned dispatcher effect

The existing real Runtime/CpuPort/X4/FatFs maintenance fixture and checked host
native store were rerun with the same one-page settings maps and accumulated
history. Five fresh-process repetitions of each paired and single scenario pass
normally and under ASan/UBSan. Their deterministic metrics match exactly across
repetitions and sanitizer modes. SD queue, sector read/write and native-call
counts are unchanged from the published baseline, as are namespace outcomes.

| Checkpoint | Generations per namespace | Owner polls before / after | Queued SD operations | Synthetic blocked ms before / after |
| --- | ---: | ---: | ---: | ---: |
| paired | 1 | 222 / 210 | 68 | 262 / 246 |
| paired | 10 | 298 / 250 | 104 | 368 / 308 |
| paired | 100 | 1,898 / 1,082 | 876 | 2,607 / 1,583 |
| single | 100 | 949 / 541 | 438 | 1,303 / 791 |

The synthetic totals include both owner scheduler delays and modeled provider
worker delays; hardware sleep remains zero. Host wall samples are retained
separately and vary with scheduling/contention. Neither those wall samples nor
the synthetic reduction establish physical card latency or a product response
budget. At 100 generations, 438 queued operations are still needed for a single
namespace checkpoint. Target latency, responsiveness and watchdog behavior remain
activation gates.

## Correctness and capacity checks

- All 25 existing real lifecycle/Runtime/X4/FatFs cases pass normally and under
  ASan/UBSan, including available/absent media, USB exclusion, live drafts,
  explicit/implicit final release, first export, typed restore, process-death
  resume, cancellation, concurrent workers and terminal retention.
- The new eight-mode native catalog test asserts at most one raw submission and
  one raw collection per step. It covers successful fusion, one BUSY refusal,
  cancellation with the fused operation queued or completed but uncollected,
  epoch/lease/media loss and retention. All modes preserve the SD bytes and
  mint no source authority on failure.
- Running that focused test against the pre-change catalog fails its required
  fusion assertion. The optimized header was restored byte-exactly afterward.
- The catalog, interrupted-catalog and checked native-restore suites pass normally
  and under ASan/UBSan. The sanitized rerun records the catalog source hash before
  and after all three suites. It does not use the preliminary overlapping run.
- The finite one-page capacity fixture still accepts 5,761 generations and
  refuses 5,762 without mutation. Peak catalog payload remains 176,972 bytes.
  This is a fixture-specific boundary, not a general generation-count contract.

LeakSanitizer is not qualified. The first focused sanitizer invocation completed
the eight test bodies, then LeakSanitizer reported its unsupported ptrace check.
That log is preserved as an incomplete run. The successful sanitizer campaigns
explicitly use `ASAN_OPTIONS=detect_leaks=0`; permanent process-lifetime controls
and the existing dispatcher fixture's `_Exit` likewise do not prove leak freedom.

## Target compilation and evidence

The existing single-job .99 SDK lifecycle probe compiles actual main, Runtime,
worker, maintenance, bootstrap and graph-pin code in both configured layouts.
Unselected lifecycle/maintenance/bootstrap translation units remain empty.
Maintenance remains 32,504 target bytes and bootstrap 16,328 bytes. In this
compile, catalog step/submit and maintenance poll local frames are 64/448/48
bytes. Local frames are not complete-call-chain stack high-water.

`evidence/catalog-poll-fusion/` contains exact source and binary manifests,
normal/sanitized logs, all repeated metrics, finite-capacity output, the negative
control, target commands/local frames and artifact hashes. The repeat helper's
historical `baseline_source_commit` identifies the original benchmark ancestry;
the compiled-source manifests and this increment's source ID identify the actual
optimized inputs. No product selection, private-backend qualification or hardware
execution is supplied by this source checkpoint.

Run `test/run_native_catalog_poll_budget_test.sh` for the focused suite. Reuse
`test/run_native_settings_maintenance_test.py` and
`test/repeat_native_settings_history.py` with the frozen .99 external pins from
the recorded manifests for the dispatcher campaign.
