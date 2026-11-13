# Explicit read-only native time sampling

`RISC_ENABLE_TIME_SAMPLE` selects `platform.time-sample@1`; the canonical C table
is `sdk/driver/RiscTimeSampleV1.h`. This is a provider dependency, not an app grant:
normal provider resolution must expose a separate product-selected facade such as
`net.time-sample@1`. Raw `platform.*` app requests remain prohibited.

The implementation reuses the actual `HttpTimeSample::Sample` packet engine in a
permanent native-owned worker. Owner callbacks only copy requests/results and poll. It
does not enable HTTP/TLS, global SNTP, credentials, automatic networking, or civil
clock/RTC writes. Product clients choose a server and retain their existing radio
service lease through checked close. Products separately decide whether/how to
commit the sampled UTC using their existing clock/alarm-edit transaction. Manual
clock, timezone and sleep policy are not changed here.

## Ownership and bounds

One session owns a copied hostname (maximum 253 characters) and a non-repeating
64-bit token. Open validates and copies the request; the worker checks connected station/IP.
Step polls completion and only changes the output on success. The
request deadline is 1..5000 ms from open. The inherited SDK DNS resolver is
synchronous: its SDK retry configuration bounds it, and request timeout is checked
after it returns. The owner never waits for that resolver; close returns PENDING
until the worker finishes and performs checked cleanup. This is not a preemptive
5-second cancellation guarantee. UDP send/receive are nonblocking, and wrong-peer/nonce rejection is bounded at eight.

The existing engine validates exact packet length, peer IPv4/port, echoed nonce,
version/mode/leap/stratum, nonzero receive/transmit timestamps, ordering, and a
unique NTP era in 2024..2099. A result is explicitly UNAUTHENTICATED (the actual
flag spelling is `RISC_TIME_SAMPLE_UNAUTHENTICATED`); 1-second resolution does not
claim accuracy or RTT compensation. A product's narrower clock range must be
checked before mutation. Completion monotonic time allows a client to reject an
aged sample before committing it.

Checked close is mandatory after success, error or cancellation. Close=PENDING
requires keeping provider/network lease and polling; only OK permits release.
The worker has an 8192-byte native stack and one bounded internal-RAM mailbox.
Startup allocation/task failure exposes CLOSED rather than falling back to blocking
owner I/O. Internal placement is checked before atomic operations. No SDK task is
force-deleted, and no app/provider pointers or callbacks cross into the worker.
New generations cannot start until the prior worker and close are acknowledged.
The task waits on notifications when idle/completed; it does not poll continuously
when no sampling is requested. Active transport steps yield at least one tick.
Descriptor-close
uncertainty permanently fences the sampler: no repeated close, provider release,
or radio shutdown. Last successful close is idempotent. CPU owner checks, native
discovery, storage custody, radio temporary-operation/cancel/leave, IQ, app exit,
restart, quiescence and light/deep sleep paths preserve the existing HTTP-style
transport ownership fences. A healthy active sample does not itself prohibit
normal storage callbacks; uncertain descriptor custody does.

## Evidence

Run `test/run_time_sample_test.sh`, then `SANITIZE=1` with the same script. The
script compiles canonical C ABI checks, the actual native sampler/HTTP engine and
actual CPU port/Runtime sources. It reuses existing HTTP tests rather than a
separate packet-parser model. Current coverage includes 190 inherited HTTPS cases,
54 sampler cases, CPU sleep/exit/radio/async cancellation/context/retention checks
and malformed backend table rejection. Mailbox tests cover blocked DNS with
responsive owner polling, cancellation at every phase, stale tokens, timeout,
Wi-Fi loss, startup OOM/internal-placement/task failure, and permanent retained
cleanup. Sanitized execution uses ASan/UBSan; this
environment runs with `ASAN_OPTIONS=detect_leaks=0` (leak checking is not claimed).

These tests are host software evidence. Selected ESP32 target build, facade and
app composition, physical server access and civil-clock commit acceptance remain
integration gates; no device command or native firmware rebuild was performed by
this isolated change.
