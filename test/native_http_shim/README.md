# Native HTTPS test seam

The production `NativeHttp.h` and `HttpResponse.h` compile here unchanged.
Only ESP-TLS, clock, allocator and Mbed TLS configuration/certificate structures
are modeled. No real network or credentials are used.

The HTTP parser is the real pinned SDK implementation, copied byte-for-byte from
Espressif ESP-IDF v4.4.7:
- components/nghttp/port/http_parser.c
- components/nghttp/port/include/http_parser.h

Their upstream MIT licensing notices are retained in both files. Target builds
use the already linked SDK `nghttp` parser, not these test copies. This seam
checks actual response parsing and transport control, but it is not an actual
TLS handshake or proof of native memory/latency/hardware behavior.

Production transport uses `esp_tls_conn_new_async` with nonblocking sockets,
TLS1.2 minimum, full compiled certificate bundle and hostname verification. The
pinned qio_opi SDK defines CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC and omits
CONFIG_MBEDTLS_HAVE_TIME_DATE. The native callback therefore preserves bundle
verification and adds certificate not-before/not-after checks using copied,
explicitly supplied UTC. It never changes system time or disables verification.
Calendar conversion is independent of the target's 32-bit time_t.

DNS resolution within the SDK's first connect step is synchronous and uses its
configured resolver retry/timeout policy. A deadline is checked after that
call, but cannot interrupt a stuck SDK function. Subsequent TLS connect, send,
and read steps are nonblocking; each reads/writes at most512 wire bytes. Product
callers must yield between AGAIN responses and display their own status/cancel UI.

IDF `esp_tls_conn_destroy` frees its handle even if socket closure returns -1.
The implementation never retries that freed pointer. It retains a poisoned
session marker, blocks radio teardown/sleep/app unload and requires a restart.
Healthy sessions preserve bound alarm-service storage; normal close frees all
native resources. SDK hidden failures and actual heap headroom remain unqualified.
