# Raw Quad SPI transport

The optional `risc_spi_quad_v1` suffix extends the complete existing `garden_spi_v1` table, including its three-wire function. Existing single-lane and three-wire operations retain their layout and behavior. Callers check the advertised extent, tag and version before reading the quad function pointers.

`display.spi` configuration version 2 adds `data2` and `data3` to the version-1 display configuration. Bus `mosi` and `miso` are data0 and data1. DC may be -1; all four data pins, clock, CS and other configured pins are validated. The physical controller is explicitly SPI2 or SPI3.

A quad provider claims the four-lane bus with `claim_quad`, uses ordinary `exchange` for one-lane command bytes, and `exchange_quad` for four-lane payloads under the same begin/end and manually managed CS. Quad transfer payloads are TX-only, 1..512 bytes. Native transfer copies the caller's bytes into the controller-owned DMA buffer and drains it within the remaining transaction budget. A timed-out queued transfer retains state until end/release completes cleanup. No CO5300 commands, display dimensions or device-specific pins are embedded in the native transport.

Quad and ordinary use cannot be mixed on a claimed bus; this initial implementation supports an exclusive quad device on that physical bus. The other physical SPI controller remains available for a separate SPI device. Transfer deadlines use the existing port budget; quad phases are additionally bounded to 8 ms. Application/frame scheduling remains the provider's responsibility.

`test/native_quad_test.inc` exercises the real Board -> Runtime -> CpuPort -> NativeSpi path against the SDK shim, on both controllers. It covers pin binding, single/four-lane frames, CS lifetime, size/deadline limits, startup faults, late DMA drain, close failures and legacy SPI/three-wire regression. The ESP32-S3 target compilation is also run in CI. These tests do not establish physical panel operation or measured wire throughput.
