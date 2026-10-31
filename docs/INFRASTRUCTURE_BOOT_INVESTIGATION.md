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

## v1.2.3 save rejection and test evidence correction

The owner confirmed that v1.2.2 now broadcasts the setup SSID and opens the
captive portal. Their screenshot shows `Use the setup page.` after Save. The
product HTTP parser compared a valid 20-byte POST request line to a hardcoded
19-byte length and returned 405 before station connection. The static setup-AP
no-internet sentence was unrelated to the target Wi-Fi network.

The prior product portal PASS output was invalid evidence: its SHA header
defined NDEBUG, disabling its assertions. The product wrapper now preserves
NDEBUG, all infrastructure test translation units reject disabled assertions,
and the old parser demonstrably fails with assertions restored. The corrected
parser and updated mobile setup guidance are covered by active assertions.
Shared Core console/radio suites did not include that header and were unaffected.
See the System Apps profile's v1.2.3 conformance amendment for exact boundaries.
Native runtime code is unchanged from v1.2.2; source identity remains verified.
Physical settings save, station join and live services are still pending.

## v1.2.4 certificate-chain failure after successful setup

The v1.2.3 device session `4f0b2456-4ed4-47bb-b92d-07b2533a3038`,
inspected through sequence 1709, advances through setup, station join, two
durable writes, and an HTTPS request. It repeatedly reports ESP error 32794
(`ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED`) and Mbed TLS magnitude 12288
(`MBEDTLS_ERR_X509_FATAL_ERROR`). `memory=0` is not an allocation failure.
This capture has zero browser capture gaps; it ends at 2026-10-31 00:13:37Z.

Both public service endpoints present the chain wildcard cantikash.chatgpt.site
-> WE1 -> cross-signed GTS Root R4 -> GlobalSign Root CA. The selected full
bundle trusts GTS Root R4 but has retired GlobalSign Root CA. With cross-signed
verification disabled, the SDK follows the extra peer certificate to the
retired root. The regression compiles the actual pinned SDK X.509/PSA and bundle
source against this captured chain and reproduces exactly -12288. Public chain
capture: GitHub Actions run 41149393016; no credentials were used.

The generic native build now enables the SDK CA-candidate callback so chain
construction can terminate at the already trusted GTS root. The pinned SDK's
previously disabled branch also references the removed Mbed TLS 4 `next_merged`
field. A source-digest-checked build transformation removes that obsolete
assignment and matches candidate allocations to the Mbed TLS allocator. The SDK
checkout and trust store remain immutable. Target and host tests compile the
same transformed source. Candidate allocations follow the existing PSRAM TLS
policy and their original errors remain visible before SDK fatal-error mapping.

The private session clock still checks all real certificates in the verified
chain. Only the exact synthetic subject/key anchor returned by the bundle's
CA callback skips a nonexistent encoded date interval. An arbitrary certificate
with empty dates does not receive this exemption. Hostname, signature and trust
checks remain enabled; existing failure flags are never cleared by the core.

The real-chain regression accepts the service fixture with zero flags and
rejects wrong hostname, expired/future leaf, expired intermediate, tampered
signature, untrusted signer, empty real-certificate dates and invalid private
clock. Enabling cross-signed verification with the old date wrapper is a
separate failing control. ASan covers 100 repeated verifications plus the first
99 allocation-failure positions. Counted allocator ownership returns to its
baseline after every attempt and to zero after PSA shutdown. LeakSanitizer
cannot inspect processes in this environment; these are explicit allocation
checks, not a claim of a successful LeakSanitizer run. These tests use portable
host cryptography, not the ESP32 hardware crypto accelerators.

The same session reports 31,016 lost radio diagnostic records. The worker was
queuing six unchanged status edges per poll while the owner was in HTTPS.
The core now coalesces unchanged ApInfo/NetifUp/IpInfo results, retains every
change, resets suppression per operation, and doubles the bounded ring to 64.
The atomic latest edge still updates on every SDK call and exposes an unchanged
probe that stalls for 100 ms. Lifecycle records and explicit overflow reporting
remain. ASan/UBSan exercises 60,000 repeated edges, changed failures/recovery,
stalled calls, new operations and intentional saturation, with diagnostics on
and off. Existing USB console admission and its 8192-byte TX buffer remain.

The application/package hashes and private pairing file remain compatible with
v1.2.3. Hardware confirmation of the new certificate path and completed live
service reconciliation remains pending; host checks do not establish it.
