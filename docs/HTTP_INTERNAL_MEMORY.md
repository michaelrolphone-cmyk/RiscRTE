# Explicit internal-memory HTTP selection

`RISC_HTTP_INTERNAL_MEMORY=1` selects the SDK internal-RAM mbedTLS allocator
and internal-RAM HTTP session/request buffers for a statically linked product
on a board without PSRAM. Initialization rejects an incompatible SDK allocator
configuration. This is a build selection, never a fallback after PSRAM failure.
Default builds retain their existing PSRAM-only policy and admission checks.

This does not port the ELF loader, paired store, metadata graph, or app-data
backend to internal RAM. Products using those facilities still require their
original memory cohort. The relay-board product statically links its agent,
uses native NVS journaling, and selects this transport policy explicitly.

Target compilation does not establish worst-case memory admission or hardware
qualification; allocation failures continue to reject that connection.
