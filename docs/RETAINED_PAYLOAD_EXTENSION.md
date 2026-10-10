# Optional larger retained application values

`RISC_RETAINED_WAKE_BYTES=512` opts a native build into a bounded retained-value
extension. The default remains 128 bytes. No capability policy or application
grant limit changes. Applications discover the tagged table tail with
`risc_retained_wake_extended`; an older/default native returns no extension.

The original API table prefix and 144-byte record remain unchanged. Legacy
read/stage callbacks still accept at most 128 bytes. The optional read_bytes and
stage_bytes callbacks synchronously copy values, accept no zero-length value,
and preserve the same invocation, identity, custody and terminal-sleep gates.
An undersized read buffer does not consume the snapshot. Invalid arguments do
not replace the pending value. Release/reacquisition invalidates copied contexts.

The native RTC envelope uses format 2 only for the explicit 512-byte build;
format 1 remains the default. A firmware/configuration change fails closed on
an incompatible RTC envelope. The committed envelope is consumed at startup,
bound to exact application/cohort identity, and checksummed over every byte.
Only successful terminal deep-sleep entry commits the pending value.

The native image increases from 828 to 1,212 RTC bytes, a 384-byte delta. A
selected product must verify total linked RTC usage, including its own records,
against the target memory budget. The linked read-only marker
`risc_retained_wake_payload_max` is exactly 512 when enabled and absent by
default; do not infer a selected configuration from the version number.

This supports larger app-owned schemas without imposing their layout on the
Runtime. An app-data reference remains an alternative, but requires the product
to account for filesystem/provider startup, access grants and wake-time I/O.
For the X4 copied Points view, a bounded 408-byte value avoids those per-minute
filesystem operations. Product-specific refresh expiry and UI remain app code.

Validation: run `test/run_retained_wake_test.sh` unchanged for the default and
with `RISC_RETAINED_WAKE_BYTES=512 RISC_APP_POLICY_ROWS=17` for the extension.
`SANITIZE=1` enables ASan/UBSan. The production Runtime/dlopen test covers a
512-byte deep-wake round trip, legacy rejection without consumption, short
buffers, ownership, stale contexts, failed preparation and complete-envelope
corruption. Physical sleep/power behavior requires device qualification.
