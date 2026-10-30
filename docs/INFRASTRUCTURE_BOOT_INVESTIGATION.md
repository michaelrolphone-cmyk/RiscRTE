# Infrastructure startup investigation and v1.2.2 corrections

The owner's 2026-10-30 v1.2 and v1.2.1 attempts did not expose the
`RiscRTE-Setup` network. The v1.2.1 device captures establish that the application
was running: runtime access, memory/node/station acquisition and bootstrap all
returned, followed by increasing `RTE_LOG lost=N truncated=0` reports. The
missing application status was not proof of an application-entry stall.

## Shared native USB console

`Console::availableForWrite()` always returned 128, the per-call write budget,
even with an 8192-byte driver buffer. `SleepDiagnostics::line()` admits an entire
record before writing it. Agent status records longer than 128 bytes were
therefore discarded even with an empty buffer. The short lost-record reports
continued to appear and concealed the actual setup failure.

The shared native Console now defaults to 8192 TX bytes in every USB profile,
including builds without stage diagnostics. It tracks accepted-byte credits and
reclaims them only when the public IDF `usb_serial_jtag_wait_tx_done(0)` query
reports full completion. This is a conservative capacity bound, independent of
the still-bounded 128-byte writes. It does not inspect private SDK state or wait
for a host. Partial drain does not reclaim credits. Wrong-core calls report no
capacity; failed teardown retains ownership and accounting. UART retains its
existing FIFO semantics and documented mutex caveat.

ASan/UBSan tests compile the actual Console and actual line sink against the
selected IDF6 public declarations. They cover 127..255-character records, stage
logs on/off, default 8 KiB bursts, full-buffer rejection, disconnect/reconnect,
drain, ownership and failed teardown. Existing USB recovery and serial transport
suites pass. These tests use deterministic driver mocks, not a physical USB host.

## Captive-portal permission

The generated infrastructure board configuration set radio `features` to 1
(station only), and infra-station validated that same value. The real Core
`Port::radioAsyncBegin` requires bit 2 for AP requests and rejected setup before
calling the native radio backend. Both the board generator/fixture and the
provider now require 3 (station plus AP). The generic Core permission gate is
preserved; unrelated profiles gain no additional authority.

A regression links the actual infra-station implementation and actual CpuPort,
reads the shipped board fixture and reproduces station-only AP denial before
any backend call. It then verifies AP admission, connection polling, cleanup
and subsequent station join with the corrected permission. The SDK radio
backend is mocked; this does not establish that the antenna is broadcasting.

## Source identity and remaining hardware acceptance

The earlier v1.2 binary had retained a stale embedded source marker from CMake.
The v1.2.1 changes made CMake watch Git HEAD and its symbolic ref and made the
image verifier reject an embedded/manifest mismatch. Those checks remain.

v1.2.2 contains concrete logging and AP-permission fixes. The infra-node package
and private service pairing pins remain unchanged; infra-station is rebuilt.
Configuration still uses the on-device portal, with no workstation credential
injection. Hardware acceptance remains: SSID appears, phone opens 192.168.4.1,
settings commit, station joins, reboot preserves settings and live service
reconciliation completes. Release metadata must not claim those observations
until the owner's device provides them.
