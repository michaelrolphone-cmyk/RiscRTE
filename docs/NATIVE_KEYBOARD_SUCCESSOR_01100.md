# Runtime 0.1.100 native qualification successor

This local maintenance increment follows source checkpoint
`4edce3e96d763277fff7d4cdcfcee59a106e981a` and the exact delivered X4 Runtime
0.1.99 source tree. It retains the loading-SDK repair, checked USB serial
restoration and 26-provider/42-grant capacity change. Production C/C++ and SDK
sources are unchanged from that checkpoint.

The selected IQ/stage target explicitly retains hardware SDMMC, including its
linked ABI witness. X4 composition selects the existing DIO/80 MHz flash,
octal PSRAM, 17 immutable policy rows, app image cache, USB PHY ownership,
512-byte retained wake payload, failure evidence, native diagnostic observer,
and X4 early-boot hook through the unchanged product composer. IQ reservations,
paired app-data geometry, HTTP and plain stage diagnostics remain selected.
Build commands set PLATFORMIO_SETTING_ENABLE_TELEMETRY=No explicitly.

This is not a canonical merge, release, or replacement for Runtime PR60
`b70cb87bdead507b9bc826f445935c5ad2b08bad`, the independently published
0.2.0 optional declarative-scene prototype based on 0.1.83. That prototype's
scene/checkpoint SDK, configurable capacity override and linked capacity
witness are not imported by this maintenance increment. 0.1.100 remains on
the 0.1.x maintenance line.

Native qualification evidence, exact invocation, source/toolchain hashes,
linked feature proofs, memory measurements and exported-symbol custody are
recorded separately. Target builds do not establish hardware reliability.
Delivered X4 0.1.50 is not modified, and no product image is assembled here.
