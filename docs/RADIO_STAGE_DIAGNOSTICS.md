# Native radio stage diagnostics

The existing automatic stage targets now report native Wi-Fi and Bluetooth
operation boundaries and failures through the same timestamped owner sink.
Runtime remains 0.1.62 for the pending combined integration. There is no new
command, performance recorder, radio policy, reconnect worker or retry loop.

Wi-Fi reports startup/configuration SDK errors with the exact numeric return
code and failed step, accepted scan/connect requests, link-state changes,
scan completion counts, timeout, numeric disconnect and scan-event reasons,
and checked cleanup results. Existing SDK log suppression is unchanged.
Controller acquisition by an external provider alone still does not initialize
Wi-Fi; actual Scan/Connect starts it.

Bluetooth reports packet-buffer allocation failure, exact controller init,
enable, callback registration, disable and deinit errors, successful on/off
states and retained uncertain initialization. Malformed packets and receive
queue overflow are recorded in a small callback-owned reason field and reported
once by the owner. No formatting or transport call occurs under the callback
lock or from the callback itself.

Only literal operation names, enum states, counts and numeric SDK reasons are
logged. No SSID, password, MAC address, IP address or HCI payload is passed to
the diagnostic formatter. State/reason tracking exists only in stage builds;
disabled builds do not call or link the timestamp formatter.

## Verification

- `bash test/run_radio_stage_test.sh`: real NativeRadio/NativeHci implementations
  against their existing SDK shims with stage diagnostics enabled and disabled.
- `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_radio_stage_test.sh`:
  the same fault/cleanup, privacy, transition and callback-lock tests with
  ASan/UBSan.
- `bash test/run_radio_test.sh` and `bash test/run_hci_test.sh`: existing native,
  owner/token, manifest binding, real Runtime/provider-graph handoff and retained
  failure fixtures.
- The `esp32s3-16mb-appdata-iq-stage` target uses the installed pinned toolchain
  and single-job build. No hardware access, installation, flash or publication.

These shims establish software behavior and message contents. They do not
qualify RF, coexistence, controller reliability or physical boot/wake behavior.
