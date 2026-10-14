# Native HTTPS PSRAM allocation

Runtime 0.2.28 addresses the Daily Paper memory rejection without
changing the application, Runtime ABI, product selection, or TLS verification.
It is not a hardware-qualified firmware. X4 composition is qualified separately.

## Integration lineage

The HTTPS-only source change is ported from `dabb73b8a57cd05fb87a7c9bd386b2d5fb232a3d`
onto delivered X4 native Runtime `6377d498a47b59dac8ad85d930983fffc6c8baa5`.
The startup test keeps the latter source's exact Port contract while adding the
HTTPS initialization cases. No unrelated newer Runtime behavior is imported.

## Cause and placement

`NativeHttp::open` previously rejected internal free memory below 80 KiB or a
largest internal block below 36 KiB. Only its own Session was PSRAM-backed; the
pinned Arduino 2.0.17 / ESP-IDF 4.4.7 mbedTLS library still defaulted to internal
allocation. Its two independent TLS record buffers are 16,717 bytes each.

The process-wide mbedTLS hook is now installed once in `setup`, after Arduino
initializes PSRAM and before bootstrap or apps can start networking. All
`mbedtls_calloc` sizes use `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, exactly as IDF's
supported external allocation mode. This also affects WPA's mbedTLS-backed
crypto, so no per-request allocator switching or global HTTP failure flag is
used. Missing PSRAM or failed hook installation disables HTTPS; neither silently
falls back to internal memory. Multiplication overflow returns null, allocations
remain zero-initialized, and both arenas use `heap_caps_free`.

The earlier Reader implementation (`src/runtime/network/PsramTlsAllocator.cpp`)
used this same hook for allocations of at least 4,096 bytes. This implementation
extends it to small software-crypto and certificate allocations because the
pinned IDF external-memory mode supports them too.

## Internal allocations and admission

ESP-TLS's native handle (2,592 bytes in the pinned S3 build), socket/Wi-Fi state,
certificate-bundle index, and hardware AES/SHA DMA buffers are outside this
mbedTLS hook. They retain the SDK's own allocators. No DMA allocation is redirected
to PSRAM, and no claim is made that networking uses zero internal memory.

The old TLS-record-sized internal guard is replaced with actual capability
probes. `prepare` holds two 1,600-byte internal/DMA blocks and one 256-byte block
while creating the real ESP-TLS handle. Failure releases partial probes and
returns `RISC_HTTP_MEMORY`. The same admission applies to redirects. These
sizes cover the pinned AES bounce buffers plus its at-most-120-byte descriptor
list for a 16,717-byte record, and SHA's at-most-128-byte scratch block. Probes
are released afterward, so fragmented heaps can pass when their real required
blocks fit. Session metadata and actual TLS allocation failures remain the
authority for PSRAM availability.

This is instantaneous admission, not a lifetime memory guarantee. Concurrent
Wi-Fi can still allocate later. In particular, the SDK's first AES interrupt
allocation contains an abort path; full low-memory hardware recovery is not
qualified by host tests. This candidate does not modify that SDK behavior.

## Failure and security behavior

TLS errors are read from the current connection's error handle, supporting
positive IDF codes, negative raw read/write results, and combined high/low
mbedTLS errors. Known allocation failures return `RISC_HTTP_MEMORY`. Generic
transport failures remain transport failures rather than being guessed to be
OOM; some SDK paths conceal their underlying allocation cause.

IDF ignores the certificate-bundle attach callback's return. The adapter now
records attachment failure in its Session and stops before sending HTTP bytes,
including when IDF subsequently reports a successful connect. Bundle checks,
hostname checks, TLS 1.2 minimum, explicit certificate dates, HTTPS-only
redirects, time/size bounds, cancellation and retained-resource behavior stay
enabled. Stage logs report arena free/largest values and connection error codes
without URLs, credentials, or content.

## Verification

- `test/run_native_tls_memory_test.sh`: all sizes external, calloc zeroing,
  overflow, no fallback, partial admission, initialization failure/idempotence,
  and one shared hook state across translation units
- `test/run_http_test.sh`: actual production adapter/parser; 101 cases including
  internal fragmentation, twenty repeated requests, each DMA/TLS allocation
  failure, redirects, bundle attachment failures and connection-local OOM
- Both suites run with ASan/UBSan as well as strict ordinary compilation
- `test/test_native_startup.py`: actual production setup; allocator before
  application startup, failure handling, existing boot routes
- Existing async fd-set, checked socket cleanup and target selection tests

Host shims do not perform real TLS handshakes. Native linking, exact product
composition and physical Daily Paper success remain separate qualification.

## Pinned primary sources

- [IDF external allocator](https://github.com/espressif/esp-idf/blob/v4.4.7/components/mbedtls/port/esp_mem.c)
- [S3 AES DMA path](https://github.com/espressif/esp-idf/blob/v4.4.7/components/mbedtls/port/aes/dma/esp_aes.c)
- [S3 SHA DMA path](https://github.com/espressif/esp-idf/blob/v4.4.7/components/mbedtls/port/sha/dma/sha.c)
- [ESP-TLS adapter](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp-tls/esp_tls_mbedtls.c)
