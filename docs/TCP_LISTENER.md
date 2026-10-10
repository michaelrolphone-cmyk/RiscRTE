# Explicit native TCP listener

This is a source continuation from recovered Runtime 0.1.106 plus the recovered
scoped-storage/export changes. It does not claim to reconstruct the unavailable
unpublished 0.1.102 source. No source version is changed by this slice.

`platform.tcp-listener@1` is a provider-only raw IPv4 transport described by
`sdk/driver/RiscTcpListenerV1.h`. Native firmware binds it only when explicitly
compiled with `RISC_NATIVE_TCP_LISTENER=1`; ordinary targets have no native TCP
backend. The separate `esp32s3-16mb-appdata-iq-tcp` target enables that flag and
retains the `risc_tcp_listener_abi=1` ELF marker. It inherits its parent partition
layout unchanged. Merely compiling the capability opens no socket.

The provider must declare the exact capability dependency. Ordinary apps cannot
acquire this raw table, even with a matching app-policy row. Applications may
instead use a separately admitted ordinary provider facade. Runtime contains no
HTTP/WebDAV interpretation, path or file policy, credentials, discovery, UI,
Wi-Fi startup or automatic listener. The foreground application and its providers
must close the transport before relinquishing their invocation or dependencies.

## Authority and capacity

The registered native backend is not a shared callable provider table. Runtime
materializes a separate table for every declared provider, fills its context
with a globally nonrepeating activation generation at provider start, and revokes
that context before unload or failed-start callbacks. Native operations receive
this broker-supplied generation as their owner key. Every client/listener lookup
requires both that owner and the independently monotonic socket handle. Copied
old tables, old handles, and handles presented through another provider's valid
table fail closed. These are native ownership checks, not memory isolation.

There is one listener and at most four accepted clients across the complete
native runtime. Multiple admitted providers may receive tables, but the second
listener attempt gets `LIMIT`. The bind IPv4 address and nonzero host-order port
must be supplied explicitly. `0.0.0.0` explicitly chooses all local IPv4 addresses;
no address/port is inferred. Multicast and reserved high address ranges reject.
No hostname resolution occurs. All caller buffers are borrowed only until return.

Accept performs one nonblocking accept attempt. Read/write perform one
nonblocking receive/send of 1..2048 bytes. Partial successes report their actual
count. `WOULD_BLOCK`, read `EOF`, `NETWORK_DOWN`, invalid/context/limit failures,
I/O failure and terminal `RETAINED` have distinct statuses. There are no retry
loops, asynchronous provider callbacks or background provider tasks. Total file
sizes and application-level timeouts belong to the caller.

## Wi-Fi, close and lifecycle

Wi-Fi must already have station connectivity and an IPv4 address. Listen,
accept, read and write recheck it without joining or reconnecting. A disconnected
station returns `NETWORK_DOWN` without attempting socket I/O; explicit close
remains available to the same live provider. Native radio leave is refused while
any listener/client remains. Active sockets block app exit, restart and all four
CPU sleep entry paths. Healthy sockets do not disable authorized storage work.

Close is checked and is **not described as nonblocking**. Clients must close
before their listener; a listener with clients returns `BUSY`. The native adapter
sets a checked 1 ms `SO_SNDTIMEO` on every created/accepted descriptor. lwIP uses
this timeout when a close cannot allocate FIN state; timeout observation occurs
on TCP polling, nominally every 500 ms, plus scheduler latency. This is a bounded
SDK close policy, not a measured wall-clock/hardware guarantee. Without the
checked timeout, lwIP's default can be 20 seconds even on `O_NONBLOCK` sockets.
The relevant [Espressif lwIP close implementation](https://github.com/espressif/esp-lwip/blob/2.1.3-esp/src/api/api_msg.c)
and the installed SDK's `lwip/opt.h` / `lwipopts.h` explain this distinction.

If timeout configuration fails, the descriptor stays retained rather than
attempting a close with an unverified bound. Any close failure is terminal:
close may already have consumed a descriptor, so the runtime never retries an
ambiguous descriptor or claims successful cleanup. Owner loss after any native
operation is also terminal; custody remains and no further socket syscall runs.
A provider generation revoked while still holding a socket fences its image and
dependencies. Restart is the recovery boundary; cleanup is never forced.

Full-cohort admission copies the native backend but rebuilds candidate Runtime
provider tables and generations. It does not rebind the live CPU port, start a
provider, accept a connection or perform socket I/O. Disabled or incomplete
backends reject manifests requiring the capability. Existing diagnostics,
app-data exports and partition definitions are preserved.

## Verification

`bash test/run_tcp_listener_test.sh` compiles the production native adapter with
an intercepted syscall boundary. It creates no host sockets and performs no
network/device traffic. It covers address/port validation, capacity, partial
operations, would-block versus EOF, wrong-owner/stale handles, Wi-Fi loss,
setup/close failures and owner loss before the next syscall. Real host-loaded
providers exercise Runtime activation generations, stale table refusal, two
provider identities, exact raw-app denial, checked failed-start retention,
cohort admission, and CPU sleep/exit fences.

ASan/UBSan also pass with `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1`; LeakSanitizer
itself cannot run under this executor's ptrace environment. Existing HTTP,
realtime, provider-module-lease, radio, deep-sleep, bound-app-data and core
Runtime suites pass.

The ordinary default-off target and the opt-in ESP32-S3 target pass single-job compiles against the installed
Arduino 2.0.17 / Xtensa 8.4.0 toolchain, using a temporary qualification config
that substitutes the already-installed official-PyPI esptool 4.11.0 wrapper for
registry package `platformio/tool-esptoolpy@2.41100.260830`. The repository pin
remains unchanged. The first stock invocation was interrupted while it remained
at the package-install announcement; it produced no package lookup or approval
failure. This qualified build is not evidence that the exact registry archive
was used. ELF inspection confirms the TCP ABI marker and native socket functions exist only
in the opt-in target. Device execution, measured network performance and physical sleep are
unrun. Nothing was flashed, published or version-stamped.
