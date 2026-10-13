# Optional device-build display backend

An explicit device build may supply `RiscCpu::selectedDisplayBackend()` and its
DisplayBackend table. Ordinary builds contain no backend. This is a linked build
input, never a filename or callback chosen by boot metadata. Chip commands and
vendor libraries remain in the device repository.

A loadable display.output provider requests device-scoped
platform.display.backend@1. CpuPort validates display.spi@2, backend compatibility,
and exclusive physical controller selection before any I/O. Open reserves the
configured clock, four data pins, CS and reset. No raw SPI claim may overlap a
live backend. Apps cannot request raw platform capabilities.

Rows are synchronous native-endian RGB565, exactly one configured row. The broker
checks owner, token, row/count and active state. Failure makes the handle cleanup
only. Failed start returns a cleanup token if native cleanup cannot finish;
failed close retains every reservation. Live backends block sleep and restart.
Healthy provider-owned buffers/resources can survive app handoffs. Device code
must copy or finish using row data before returning and release every native
resource before close succeeds.

The host native SPI runner includes broker ownership/failure tests. These tests
exercise software with a backend model, not vendor electrical behavior.
