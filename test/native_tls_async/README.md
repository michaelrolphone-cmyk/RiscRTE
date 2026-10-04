# Pinned SDK asynchronous-connect regression

Copyright Espressif Systems, Apache2.0 (included). `low_level_conn.inc` is the
unmodified `esp_tls_low_level_conn` function from the IDF commit embedded in
Arduino2.0.17's ESP32-S3 SDK configuration:
https://github.com/espressif/esp-idf/blob/38eeba213a/components/esp-tls/esp_tls.c

Full source SHA256: 0fc28212f5122f58b0fb98c1798afad478dabb09c771559a3e27ddd4b953fe3e.
Function excerpt SHA256: 0a4e06498da0677f49b2e7f94b0fa399ed22b17e3a3682a13df8ccac2d928c4b.

The test uses real POSIX select() and a ready pipe as a controlled socket-readiness
model. It reproduces the unchanged SDK's cleared-fd-set stall, then proves that
re-arming its owned descriptor fixes the next poll. No actual TCP/TLS handshake
or device execution is claimed. The production NativeHttp shim separately checks
1,5 and100 pending polls, re-armed read/write sets and invalid-socket rejection.

Run `bash test/native_tls_async/run_async_fdset.sh`.
