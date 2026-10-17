# Bounded serial session custody

This change repairs a source-reproduced liveness gap. It does **not** identify
the cause of the owner's physical Waveshare 0.1.59 PATH timeout. The physical
log lost 154 records, and the owner reports that the display did not sleep.
No device reset, flash, retry or SD mutation was performed during this work.

## Reproduction and contract

At Runtime `48229bd1593a2e24e95bf4537b495ee615990fd3`, an actual native USB
host fixture completes HELLO, both namespace replies and drains output. The
endpoint then reports `pending=0 exit_safe=1`; the actual CpuPort admits terminal
Deep before the next PATH, with no provider calls. Connected cable-only controls
remain sleep-safe. This is a host/source result, not physical attribution.

An established session now owns bounded protocol RAM across reply gaps, including
CLOSE and COMMIT. An owner checkpoint expires it after the existing 30-second
inactivity budget; disconnect/shutdown release idle protocol custody immediately.
Live handles still require their exact safe owner boundary for cleanup. ABORT
ends a transfer, not its session. Expired sessions reject stale requests with
protocol error -10; clients must establish a fresh HELLO rather than replay
uncertain mutations. A cable by itself does not acquire custody.

After bounded service dispatch, Runtime reports CHECKPOINT_BUSY while native
sleep services remain active. A command may have completed even with BUSY.
Shared Home must continue rendering/input and defer automatic idle on that result;
the companion Watch change is `32f53e8ca58f58f0cec1f652ab56eee7e1a02a46`.
Manual sleep policy is unchanged. A new request arriving after the settled Home
checkpoint can still cause one ordinary pre-sleep refusal; the companion fixture
covers recovery and HELLO/NS/PATH deadlines for this race.

## Qualification boundaries

- Protocol unit regressions cover session gaps, 29,999/30,000 ms expiry, wrap,
  stale request rejection, fresh HELLO, ABORT, disconnect and shutdown.
- The actual native USB adapter fixture covers six fragment sizes for a full
  48-byte PATH chunk, metadata under unavailable storage, all CPU sleep gates,
  USB/restart custody and cable-only controls.
- The actual Runtime graph/provider fixture covers the BUSY activity signal,
  completed replies, expiry and recovery without terminal retention.
- Existing Wave/X4 source fixtures retain their frozen product pins and matrix
  workflows. Their endpoint expectations now distinguish a healthy session
  from an open file and verify provider-free idle expiry.
- The external scheduling probe uses actual Runtime, native Settings lifecycle,
  CpuPort and Wave SD/FatFs with modeled sectors/worker scheduling. It includes
  8 ms host turns and a size-matched 136,960-byte prior-file read (2,854 READs),
  followed by replacement/create/readback and sibling preservation. It is not
  the complete current browser's 45-file install and does not execute Home or
  target instructions. Its receipts identify all source inputs.

The first three scheduling attempts exposed harness assumptions: a single large
seed write, interpreting READ's cumulative offset as chunk length, and expecting
BUSY at 50 ms cadence despite successful STAT at 3,800 ms after added host turns.
Those logs are preserved; none established a product PATH failure.

Current browser v40 request/lifecycle trace and physical acceptance remain
separate evidence. Ordinary v38 post-flash flow performs HELLO/NS immediately
before preflight; historical payload-first flow renews HELLO after confirmation.
That source evidence does not prove current v40 behavior or sleeping-port recovery.

## Current checkpoint

All five normal scheduling cases passed. Service cadences 8/16/32/50 ms had
maximum response delays 608/1216/2432/3800 ms respectively. All four read every
one of the 136,960 prior-file bytes and completed replacement/create/readback.
An induced worker stall returned BUSY at 4,000 ms. All five ended without
retention. The supplied compressed receipt contains exact input hashes and
compilation commands. The Runtime provider suite passed all16 cases, including the new session
activity/expiry/recovery case. Sanitized scheduling and broader product suites
remain pending at this source checkpoint; the normal native USB receipt predates
the companion Runtime BUSY signal and is specifically the session-layer proof.

Additional owner evidence says the lit clock frame stopped advancing. The RTC
was set manually to an arbitrary time, so its displayed value is not UTC timing
evidence. A lit frame therefore does not establish live Home execution. This
strengthens investigation of retention/owner scheduling/app progress loss, while
remaining insufficient to attribute the physical PATH failure to one branch.
The requested firmware flash → manual reset → reconnect confirmation → payload
order is unchanged.

## Follow-on host qualification

The compact ASan/UBSan scheduling run passed all five cases against clean
`efb9ad2158282c572819bc0e839e0f875e5fee41`; all256 recorded input hashes were
rechecked unchanged. A preceding sanitizer link ran out of scratch space before
executing a test. Both the failure and the fresh successful run are retained.
Only debug information changed from `-g1` to `-g0`; instrumentation stayed enabled.

Important coverage limit: the long-preflight fixture holds an atomic grant through
the sequence. This permits accepted discovery to progress but prevents admission
of new native maintenance. Its result is not a complete Home scheduling proof.

A separate fixture closes discovery-era data grants before editing, then admits
an actual dirty native edit at four boundaries: before HELLO, between NS/PATH,
with an ordinary READ handle open, and after CLOSE. All four normal and all four
ASan/UBSan cases passed. PATH replies were immediate; maximum storage response
was53ms. An open ordinary handle caused a clean maintenance refusal; the edit was
acknowledged after close and a later explicit grant boundary. None retained
Runtime, native Settings or the serial provider. The earlier fixture setup failures
(missing relative include, legacy exact-poll assumption, and unreconciled native
source under an old data grant) are preserved and are not product failures.

Actual Home source separately reopens ordinary Settings KV approximately every
second for broadcast policy; atomic ownership is not a permanent Home draft.
The source-backed cadence probe also shows that a stale valid civil-clock snapshot
can freeze clock digits while frames/input/checkpoints continue. A retained
checkpoint instead ends subsequent app progress immediately. Neither modeled
case identifies the physical failure. Current browser v40 trace remains awaited.

The publication union preserves all16 unrelated catalog-selection additions
from current main932000df. Diagnostics investigation is a separate checkpoint.
No successor firmware or physical acceptance is claimed by this source PR.
