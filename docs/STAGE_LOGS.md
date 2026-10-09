# Automatic plain stage logs

Runtime 0.1.60 provides plain diagnostic statements at the actual boot, app and
provider boundaries. Build with `RISC_STAGE_LOGS=1`, or use:

```sh
PLATFORMIO_SETTING_ENABLE_TELEMETRY=No pio run -e esp32s3-16mb-appdata-stage -j 1
PLATFORMIO_SETTING_ENABLE_TELEMETRY=No pio run -e esp32s3-16mb-appdata-iq-stage -j 1
```

These targets do **not** enable `RISC_PERFORMANCE_TRACE`. No command, record
decoder, interaction summary or replay is needed to see statements. Existing
optional performance APIs and the `perf` command remain compatible if separately
enabled. Normal targets do not evaluate stage-log arguments, sample their clock,
format strings, or retain stage-log counters.

Typical output (illustrative timestamps):

```text
RTE_STAGE us=930000 app launch-request file=settings.elf result=accepted
RTE_STAGE us=931000 app unload begin file=/bootfs/springboard.elf
RTE_STAGE us=934000 app unload end file=/bootfs/springboard.elf result=ok elapsed_us=3000
RTE_STAGE us=934100 app load begin file=/bootfs/settings.elf
RTE_STAGE us=960100 app load end file=/bootfs/settings.elf result=ok elapsed_us=26000
RTE_STAGE us=961000 provider start begin id=example-provider
RTE_STAGE us=981000 provider start end id=example-provider result=ok elapsed_us=20000
```

Each statement has a monotonic microsecond timestamp from `esp_timer_get_time()`.
Boot begins with reset reason, wake cause, and the saved `setup_start_us` timestamp.
It logs bank selection, filesystem mount, app-data preparation, provisioning
action/reason, metadata preparation and its outcome. `boot metadata-ready` means
metadata/board validation completed; it does not assert that an app or display
is ready. Driver activation and app entry follow. ROM/bootloader work before
`setup()` is outside this window. Timestamps restart on a fresh boot.

App statements identify filenames for accepted/invalid-path launch requests,
ELF loading, optional initialization, entry/return and unload. Provider statements
identify IDs for loading and start, with success/failure and elapsed time.
Failed loading includes the existing diagnostic reason. Demand activation
explicitly lists selected providers deferred until acquired. A deferred provider
is not reported as started. Stream-bind/lease rejection is reported as skipped
start, with its reason. Actual application radio preferences and panel stages
remain owned by their apps/providers and can use the existing diagnostic callback.
No credential contents are added to Runtime statements.

Stage builds read provider failure details into a bounded 512-byte stack buffer
and emit the original text in numbered 80-character parts before quiescence.
Each part repeats the provider ID. This preserves details beyond the ordinary
112-byte callback buffer and shorter retained error string. A completely filled
511-character payload emits `source-buffer-full=511 report-may-be-truncated`;
the callback API cannot distinguish an exact fit from a longer truncated report.
Ordinary builds retain their original callback bound and class layout.

Elapsed times are inclusive and include the begin statement's logging cost.
Provider load includes its nested provider start. Do not add these overlapping
intervals. A begin without an end may mean failure, retention, reset or lost
output; it is not a zero-duration success. An app entry is not display completion.
Software touch timestamps and provider completion timestamps do not establish
the physical touch edge or when a person perceived the display.

## Bounded best-effort transport

Statements call the existing owner diagnostic sink directly. There is no new
payload queue, recorder or background worker. Text is at most 255 bytes plus a
newline; oversized formatted statements end in `...`. Ordinary app diagnostic
lines keep the same bound. Each USB write is at most 64 bytes. Whole-line capacity
is checked first, and positive short writes progress through that fixed payload.
A zero write or exhausted capacity ends the attempt immediately. There is no
waiting for USB, host attachment, or buffer space and no USB boot dependency.
The pinned transport remains the single owner-task producer with zero timeout.

Stage builds using hardware USB preallocate an 8192-byte TX ring through the
pinned HWCDC driver's existing `setTxBufferSize()` before its first `begin()`.
This absorbs a bounded startup burst without another logging queue. It costs
7936 additional heap bytes for payload capacity over the ordinary 256-byte ring
(plus allocator/driver overhead). Sleep recovery repeats the preallocation after
`end()` frees the old ring. It never resizes a live ring. Allocation failure allows
the driver's ordinary smaller-buffer fallback; the boot statement reports that
failure. Normal targets, UART and TinyUSB keep their previous buffer behavior.
Stage/HWCDC statements are admitted to this existing ring even while CDC is not
ready. The original timestamped text is sent automatically when the host starts
reading. Whole-line capacity is checked before admission and again for each
write, preventing the driver's disconnected FIFO policy from evicting earlier
startup bytes. No extra queue, wait, command or recorder is involved. A ring that
fills while disconnected drops later statements and counts the loss. Pending
bytes remain volatile: reset or the existing sleep-recovery `Serial.end()` can
discard them. The ring does not guarantee host delivery or retention across sleep.

In stage builds, `RTE_LOG lost=N truncated=N` reports cumulative counters since
diagnostic startup when polling next has capacity. `lost` counts attempted lines
that were not accepted (including insufficient capacity,
active explicit dump, and reentry). `truncated` counts formatting truncations or
partially emitted lines. These are separate loss events, not necessarily unique
statement counts: one oversized statement may also be dropped by transport.
Counters saturate at `UINT32_MAX`. Abandoned partial live lines are terminated
before another line or dump; their old text is not retained or retransmitted.
An interrupted loss report is retried with current cumulative counts.

Existing explicit `diag`/`perf` dumps remain framed and are not interleaved with
live statements or loss notices. Live attempts during a dump count as lost;
the notice follows the dump. UART/TinyUSB and non-stage builds retain their
existing disconnected-host behavior. A capture with loss/truncation is incomplete, and even zero
reported loss cannot prove the host captured every byte.

## Software verification

`test/run_stage_log_test.sh` checks automatic formatting, complete app lines,
missing host, full-line capacity, 1/7/63-byte short writes, zero writes,
truncation/loss notices, owner/reentry, command arbitration, and disabled code.
It runs with each independent combination of sleep diagnostics and performance
recording enabled/disabled. `STAGE_LOGS=1 test/run_performance_runtime_test.sh`
checks real host-module lifecycle statements, cross-app order, failed-child
fallback, named successful/failed provider starts and demand deferral, both with
the recorder configured on and off. Sanitizers cover both suites. These checks
and target builds do not qualify physical latency, radio reliability or hardware.
