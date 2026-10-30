# Additive HTTP client v2

`platform.http-client@2` is an explicit versioned native facade, preserving v1 registration and layout. `sdk/driver/RiscHttpClientV2.h` is the C ABI. The native implementation owns copied URL, headers and request bytes through close, and admits one shared v1/v2 TLS session. GET/POST,16KiB request,32KiB response,4KiB aggregate headers and512-byte transfers are bounded. Redirects are not followed. Selected ETag, Last-Modified and retry metadata and all completed HTTP status classes are exposed without confusing them with transport failure.

V2 DNS uses a native-owned single mailbox/task. Closing while resolution is active reports AGAIN, then retained failure at the deadline if quiescence is unknown. No worker accesses ELF-owned memory. Numeric resolved addresses retain original-hostname TLS verification. V1 DNS behavior is unchanged. Certificate checking and first-request SNTP bootstrap remain native. The product caches the resulting UTC/monotonic anchor; scheduling uses monotonic time.

Ordinary return values, terminal pending bytes and close ownership are verified by `test/run_native_http_test.sh`, `test/run_native_http_idf6_test.sh` and `test/run_native_dns_test.sh`. These are host shims, including compilation of the actual IDF6 branch, not a physical TLS/network certification.

`memory.heap@1` adds query kind STACK_HEADROOM without changing its struct layout or existing values. The S3 port reports the calling owner task's historical minimum unused stack bytes. Existing heap, binding, storage and retained-lifetime suites remain applicable. The runtime exports required pure compiler arithmetic helpers for independently linked product ELFs; no product policy or credential is linked into the native runtime.
