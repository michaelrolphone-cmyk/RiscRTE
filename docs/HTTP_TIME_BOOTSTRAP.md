# HTTP-scoped first-start time sampling

`RiscHttpTimeV1.h` adds a tagged, append-only suffix to the existing provider-only
`platform.http-client@1` table. It introduces no capability, clock authority,
raw-socket interface, default server, background task, or caller callback.
Providers must use `risc_http_client_time_from_v1` before calling it. The accessor
checks the complete size, API version, tag, suffix version, all original
callbacks, and the new callback. CpuPort publishes the suffix only when its
backend supplies a valid one; older providers/backends keep their old prefix.

The new `open_with_time(context, request, explicit_server, handle)` requires
`request.utc_seconds == 0`. Ordinary `open` still requires UTC in 2024–2099 and
rejects zero. The request URL and explicit DNS hostname (1–253 bytes, labels
1–63 bytes) are copied before return. Runtime supplies no public time-server
selection. Time-server policy remains with the calling product.

A successful open returns an ordinary HTTP handle immediately. `read` advances
sampling, then the existing TLS/HTTP state machine; `info` and `close` work
throughout. The session owns one nonblocking UDP socket, one selected IPv4 DNS
answer, one 48-byte SNTP request with a fresh 63-bit random transaction nonce,
and bounded copied state. DNS uses the pinned SDK's existing synchronous
resolver style. It cannot be interrupted inside the SDK call, but its elapsed
time consumes the original request deadline and is checked before any socket
or TLS connection is started.

Sampling is additionally capped at five monotonic seconds from open and eight
unrelated/stale datagrams. A matching response must be exactly 48 bytes and
match the selected source IPv4 address, UDP port 123, and full 64-bit origin
nonce. Only server mode, NTP versions 3/4, synchronized leap states, and strata
1–15 are accepted. Receive/transmit timestamps must select exactly one NTP era
within 2024–2099, remain ordered (including fractional seconds), and differ by
at most five seconds. The 2036 era rollover uses bounded integer conversion,
independent of `time_t`, the RTC, and any existing wall clock.

The UDP socket must close successfully before TLS allocation/connection. A
failed or ambiguous close keeps the same HTTP handle and native ownership,
makes `safe` and `idle` false, and is never retried against a possibly reused
descriptor. The caller must retain its provider/radio dependencies. Ordinary
cancellation or sampling failure remains explicitly closeable. No UDP socket
is left open alongside TLS, including redirects.

A sample anchors UTC to the monotonic instant at which sampling completes.
Sampling does not reset the request deadline and is not added twice to the TLS
clock. The existing certificate-bundle verifier, hostname checks, TLS 1.2
minimum, and certificate not-before/not-after checks remain in force.

SNTP is unauthenticated. Matching source and nonce reject unrelated or replayed
responses; they do not protect against an on-path attacker or a dishonest time
server. This is a session-scoped first-start date input, not authenticated time
or clock synchronization. No `settimeofday`, `adjtime`, global SNTP service,
NativeRealtime seed, wall-clock write, or RTC write is used.

## Verification

- `bash test/run_http_test.sh`: C layout/accessor tests, production HTTP and
  sampler with syscall shims, and CPU forwarding/retention/legacy-prefix tests.
- `SANITIZE=1 bash test/run_http_test.sh`: the same suite under ASan/UBSan.
  In ptrace-based executors, use `ASAN_OPTIONS=detect_leaks=0`; native allocation
  and socket ownership are still checked explicitly by the modeled boundary.
- `python3 test/compile_native_http_target.py --packages <installed-packages>
  --output <directory>`: real pinned ESP32-S3 C ABI, production HTTP sampler/TLS,
  CpuPort and NativeHardware integration object compilation, plus an HTTP-object
  undefined-symbol audit forbidding wall-clock mutations, global SNTP, RTC, and
  NativeRealtime references.

These checks do not link or flash firmware, contact a real time server, exercise
real network conditions, or qualify hardware. The provisioning request queue,
NVS quarantine, and global mutation gates are unchanged by this extension.
