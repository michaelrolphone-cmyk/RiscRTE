# Bounded performance tracing

Build Runtime with `RISC_PERFORMANCE_TRACE=1` to enable the optional recorder.
Normal builds default to disabled and omit recorder storage on ESP32. Instrumented apps remain compatible with older
Runtime tables: include `sdk/app/RiscPerformanceV1.h` and call
`risc_perf_trace_v1(api, id, phase, value)`, which checks version, suffix size and
callback presence. The callback is appended after `retain_invocation`; existing
ABI field offsets and version remain unchanged.

## Shared interaction convention

| Phase | Meaning | Value |
| --- | --- | --- |
| 1 | Begin interaction; requires ID zero, returns fresh ID | 1 touch/app launch, 2 swipe/panel, 3 other |
| 2 | Gesture/touch recognized | App-defined numeric detail |
| 3 | Action dispatched | App-defined numeric detail |
| 4 | First draw starts | App-defined numeric detail |
| 5 | Present submitted | App-defined numeric detail |
| 6 | Presentation/interaction complete | App-defined numeric detail |
| 7 | End correlation after recording this event | App-defined numeric detail |
| 8 | App milestone/counter sample | App-defined numeric detail |

For phases 2–8 and 60/61, ID zero inherits the current interaction. A nonzero explicit ID
must equal the current interaction or is rejected. Beginning another interaction
replaces the previous ID. IDs survive app unload/load and invocation changes;
complete does not clear the ID, while end does. An app can use inherited zero for
its first draw following a launcher handoff, then end after completion. Zero
return means disabled/rejected or a recorded phase without an active interaction.
Apps may emit phases 1–8 and span phases 60/61; Runtime owns the remaining namespace.
Record milestones rather than movement samples. Present submission measures the
software boundary, not the time pixels appear on the panel. Failed launch and
unload events should be inspected before interpreting missing draw milestones.

Span phases 60 (begin) and 61 (end) use `value` as a stable app-defined tag;
SDK names are `RISC_PERF_SPAN_BEGIN` and `RISC_PERF_SPAN_END`. Match by interaction
and tag with nested LIFO ordering. For example, app tag 1 can identify promotion
and tag 2 native recovery inside it. End minus begin timestamp is inclusive
elapsed time: do not sum parent and child durations. The recorder does not enforce
pairing or keep a span stack; overwritten endpoints yield incomplete spans. Tags
are numeric and never app pointers. These semantics belong to apps, not firmware UI.

## Runtime evidence

Records contain sequence, monotonic microseconds, interaction ID, invocation ID,
phase and a numeric value. Invocation phase 9 identifies a new app invocation with
an FNV-1a subject hash over at most 128 bytes (not a security/file-integrity check).
Invocation increments are independent of interaction correlation.

| Phase | Runtime meaning |
| --- | --- |
| 10/11 | Prepare begin/exit |
| 12/13 | App load begin/exit |
| 14/15 | Init begin/exit |
| 16/17 | Entry/return |
| 18/19 | Unload begin/exit |
| 20/21 | Launch requested/app load failure |
| 22/23 | Provider acquire begin/exit |
| 24/25 | Provider start begin/exit |
| 26 | Scheduling waits (aggregate; value is requested milliseconds) |
| 27 | Provider polls (aggregate; value is call count) |
| 28/29 | Metadata read begin/exit |
| 30/31 | Parse begin/exit |
| 32/33 | Native provider/metadata read waits (aggregate) |
| 40/41 | ELF read begin/complete (complete value is byte count) |
| 42/43 | ELF map/relocate begin/exit (exit value is result) |
| 44 | ELF failure (numeric result) |
| 45/46 | ELF structure parse begin/complete |
| 50 | Setup begin |
| 51/52 | Bank selection begin/exit |
| 53/54 | Mount begin/exit |
| 55/56 | App data begin/exit |
| 57/58 | Provision begin/exit |
| 59 | Boot ready |

An exit phase includes failed exits: it does not assert success. Explicit failure
phase 21 and existing runtime error diagnostics provide the result. Begin-phase
cumulative durations survive ring overwrite. Nested durations overlap and must
not be summed as exclusive time. App scheduling wait durations include the port delay and diagnostic polling;
provider polling is recorded separately. Requested milliseconds are separate from
observed microseconds.

## Storage, replay and overhead

The owner-task-only recorder uses a 128-record overwrite ring, 64 saturating
phase counters and 64 each of cumulative aggregate values/durations. Ring
records increment phase counts; their numeric values remain per-record and are
not summed. Scope durations accumulate under the begin phase. Aggregate waits and
polls do not consume ring slots. Recorder state is ordinary RAM, cleared at
configure/boot; it is not a retained boot history. IDs, sequence and counters are
bounded; IDs/sequences are not reused at exhaustion. Dropped ring records appear
in `lost`. Phase counts include overwritten events. Aggregates have no individual
timestamps, so they show total repetition and elapsed cost rather than every wait.

On the diagnostic transport send the exact line `perf` followed by LF or CRLF.
`RTE_PERF` schema 1 reports a snapshot, ordered ring records, all 64 counters and
an end marker. A dump cannot be restarted while active. Replay formats only during
polling, writes at most 64 bytes per poll after checking available capacity,
preserves partial writes, and abandons its snapshot on disconnect. Recording
never allocates, formats, or accesses USB. The transport must honor its advertised
writable capacity. Read the complete begin/end-framed output before analysis.

At host ABI sizes the recorder consumes 5,440 bytes and replay 5,744 bytes (target
padding may differ). Disabled recording does not call the clock or owner predicate.
For each ring insertion, two clock samples measure the insertion interval;
`overhead_min_us`, `overhead_max_us`, `overhead_total_us`, and `overhead_count`
report this sampled cost. It excludes owner gating and post-sample accounting,
and cannot fully measure both clock-call costs. Microsecond quantization can
produce zero. Scope timing adds clock calls; aggregation is not included in these
overhead statistics. Use an external benchmark for complete call cost.

`bash test/run_performance_recorder_test.sh` exercises ordering, correlation,
disabled/rejected calls, cumulative timing, overflow/saturation, size gating,
exact command parsing, partial writes and disconnection. Its external host timing
reports mean complete `emit` call cost including two real steady-clock timestamp calls over one million iterations. This is a regression
and overhead smoke measurement, not ESP32 hardware qualification. Repeat on the
target with its real timer before drawing numerical latency conclusions.

## Integration and attribution

Build `pio run -e esp32s3-16mb-appdata-perf -j 1` for the opt-in candidate;
other existing targets retain disabled recording unless explicitly compiled
with `RISC_PERFORMANCE_TRACE=1`. The callback still exists but returns zero
when disabled. Target clock is `esp_timer_get_time()` in microseconds.
Startup starts at `setup()`; ROM, bootloader and earlier static initialization
are outside the measured window. State is volatile and clears on restart.

Boot phases: 50 setup entry; 51/52 committed-bank selection; 53/54 filesystem
mount; 55/56 app-data preparation; 57/58 provisioning; 59 metadata/board ready.
Phase 26 aggregates app-yield calls/requested milliseconds/actual microseconds;
27 aggregates provider polls; 32 aggregates provider native waits;
33 aggregates metadata read waits (value is requested RTOS ticks).
These repeated events do not consume ring records. Scope totals are inclusive;
provider waits nested in provider start and parsing nested in metadata reads
are attribution components, not additional time to add to their parent.
ELF structural parse45 follows read40; relocate42 includes code publication.
Failed phase44 terminates
any open loader phases. Partial traces after overwrite cannot prove a complete
interval; consult `lost` and cumulative counters.

Phase9 values identify app paths; phase22/23 identify requested provider IDs
(or capability for untargeted acquire); phase24/25 identify started providers;
phase28/29 identify metadata paths. These values are FNV-1a over at most128 name
bytes, solely compact diagnostic labels: collisions are possible, and a
matching number is not proof of identity. No file bytes are hashed or checked.

Recorder plus replay use 11,184 bytes of fixed storage on the host layout.
The enabled recorder samples insertion overhead in microseconds; this excludes
owner checks and some timing/statistics bookkeeping. The external host benchmark
includes the callback, owner checks, both real steady-clock calls and bookkeeping.
Neither is a hardware latency qualification. Target `perf` reports measured
overhead min/max/total/count; zero samples at microsecond resolution are not
proof of zero cost.
