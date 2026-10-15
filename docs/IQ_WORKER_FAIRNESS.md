# IQ worker scheduler fairness, Runtime 0.3.5

## Failure and exact source evidence

The T069 physical report says a 256-pair RF capture succeeds, then about five
seconds later the task watchdog reports `IDLE0` starvation, CPU0 `iq-envelope`,
and CPU1 `loopTask`. This is a watchdog cold reset, not a failed first capture.

The published .43 native ELF has SHA-256
`c2bb3f7447c40b581e7b3b3f43e05d0fba836a31e66c549ccf748dd7f882503a`
and Runtime source `a2c2c9fa692b0b36f9750d44fac807c9149b9434`. The pinned
Xtensa addr2line tool maps `0x42085154` to `task_wdt_isr`, and `0x42037b71` to
`NativeRadioIq::workerRun` line 99. The intervening address `0x4399ed14` is
outside native ELF symbols; no dynamic provider symbol is inferred from it.
[Exact symbolization](iq-worker-fairness/exact43-backtrace.txt) is retained.

The same loop is unchanged in the repair base
`54461e8a7f61b28600b8ec8dcaf27b12cd45e829`. It runs at priority 3 on CPU0.
`ulTaskNotifyTake(pdTRUE,portMAX_DELAY)` clears accumulated notifications, but
returns immediately whenever at least one is pending. If captures consume at
least the timer period, every iteration can begin with a pending notification.
The task then never blocks, so CPU0's lower-priority idle task cannot run.

## Correction and preserved lifecycle

After each completed callback, the worker calls `vTaskDelay(1)`. This places it
in the blocked state until the next RTOS tick even when notifications are
already pending. A plain task yield, watchdog reset, watchdog removal, or a
larger timeout would not provide this scheduling property and is not used.

Timer notifications still coalesce. Each callback receives the actual monotonic
start time; missed periods are not fabricated as regularly spaced observations.
Under overload the callback rate decreases to leave real scheduler time. No
RF parameters, provider buffer layout, ABI, capture task affinity/priority,
PHY references, or watchdog configuration changes.

The existing stop path stops the timer, requests stop, wakes the worker, and
waits up to 100 ms for the join semaphore. A request during a callback or the new
delay retains the task, callback, argument, timer, semaphore, PHY and provider
lease until join completes. It does not start another callback after observing
stop, and does not force-delete executing provider code. The delay adds at most
one scheduler tick to that join. A single callback must still return within its
own bounded contract; this repair cannot preempt a hung native callback or
promise that unrelated higher-priority tasks will let the idle task run.

## Production-source regression

`test/native_iq_worker_test.cpp` executes the real `NativeRadioIq.h` worker with
a deterministic FreeRTOS/timer model. Its 3,001 callbacks each consume 2 ms under
a 1 ms timer, exceeding the five-second failure window in virtual time.

- Before: 6,002,000 us uninterrupted worker execution; zero scheduler blocks.
- After: 2,000 us maximum busy interval; 3,001 genuine scheduler blocks.
- Accumulated notifications coalesce; callback timestamps follow actual work.
- Underload preserves the maximum 100 ms timer interval.
- Stop during callback, during delay and before first deadline does not permit
  another capture. A timed-out join preserves all callback/resource custody.
- Repeated start/stop, invalid requests, semaphore/timer/task creation failures,
  timer-start rollback, failed-join retention/retry and late wake are exercised.

[Before](iq-worker-fairness/overload-before.txt) and
[after](iq-worker-fairness/overload-after.txt) results are checked in. The new
regression fails on unchanged production source and passes after the fix,
including ASan/UBSan. Local LeakSanitizer needs `ASAN_OPTIONS=detect_leaks=0`
because this executor uses ptrace; that is a test-environment limitation, not a
production setting.

The pre-existing full IQ suite also failed at `radio_iq_port_test.cpp:45` on
unchanged base source. Its fixture still treated a per-output static GPIO hold
as whole-device sleep, contrary to the already-integrated scoped-pad contract.
A separate test-only correction supplies an actual output pin and native hold
stub, permits that unrelated hold during active IQ, and continues to require
retention during uncertain cleanup. All real sleep-entry checks are unchanged.

## Build and qualification boundary

Run:

```sh
bash test/run_native_iq_worker_test.sh
SANITIZE=1 bash test/run_native_iq_worker_test.sh
bash test/run_radio_iq_test.sh
SANITIZE=1 bash test/run_radio_iq_test.sh
```

The production Watch native profile compiles with the pinned Xtensa 8.4.0 / IDF
4.4.7 source in a single-job build and passes the mandatory final-ELF IQ bank and
DRAM/IRAM alias proof. Product-owned profile files are copied unchanged for that
check; they are not introduced into Runtime source by this patch. The standalone
native ELF is not a complete deployable product image.

Physical RF behavior, capture cadence/quality, prolonged Waterfall operation,
stop/restart/exit, idle watchdog recovery, and sleep/wake remain unverified on
hardware. This source repair does not claim hardware acceptance and performs no
serial access, flashing, merge, tagging, or release.

Version 0.3.5 was selected on 2026-10-15 after reading all 132 live branch/tag
source versions and checking open PRs plus exact issue/PR claims. 0.3.4 was
already used by `fix/t023-wave-core-profiles` at
`4f5733289de0e9085f7861f39ea39197238c59c2`; no 0.3.5 claim was found.
