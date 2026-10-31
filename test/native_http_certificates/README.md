# Real certificate-chain regression

`IDF_PATH=<pinned SDK> bash test/native_http_certificates/run.sh` builds the
actual pinned Mbed TLS 4, PSA crypto and ESP certificate-bundle implementation.
Use `BUILD_DIR` to keep a reusable out-of-tree build. ASan defaults on; the
container requires `ASAN_OPTIONS=detect_leaks=0`, so an explicit counted allocator
checks ownership and cleanup, including injected failures.

`service-chain.pem` contains only public certificates captured by Actions run
41149393016 from the two infrastructure service hosts on 2026-10-31. Its test
clock is fixed within the captured leaf's validity interval. No private keys or
pairing credentials are part of the fixture. An untrusted test key/certificate
is generated in the temporary build directory.

The before build reproduces the device's -0x3000 error without cross-signed
verification. The after build uses the production candidate tracking and
private-clock validator, then exercises negative controls and cleanup. The SDK
compatibility source is materialized by the same digest-checked script used by
the target build. Shim headers replace ESP logging/error definitions only;
cryptography, parsing, bundle lookup, chain construction and callbacks are real.
The bignum/ecp forwarding headers expose the SDK's portable private headers to
its standalone host CMake build. Target hardware acceleration is not exercised.
