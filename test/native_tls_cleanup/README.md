# Checked native TLS cleanup

SDK function excerpts are Copyright Espressif Systems, Apache2.0 (included),
from the pinned IDF38eeba213a and MbedTLS2b8e772fc1cb0732cda3bae7d1e9d6f4cfaf63d9.
Primary sources:
- https://github.com/espressif/esp-idf/blob/38eeba213a/components/esp-tls/esp_tls.c
- https://github.com/espressif/esp-idf/blob/38eeba213a/components/esp-tls/esp_tls_mbedtls.c
- https://github.com/espressif/mbedtls/blob/2b8e772fc1cb0732cda3bae7d1e9d6f4cfaf63d9/library/net_sockets.c

The exact SDK cleanup bodies run with synthetic fd calls. Control reproduces
silent established-TLS close failure; the adapter checks close once, detaches
both fd fields before destroying SDK state, never retries a freed/ambiguous
handle and keeps terminal poison on any failure. This does not open real sockets.
The runner verifies its adapter excerpt is byte-identical to production's body.
The nativeHTTP and source-wired update suites separately call actual production
cleanup with a wrapped lowest-level socket-close function.
