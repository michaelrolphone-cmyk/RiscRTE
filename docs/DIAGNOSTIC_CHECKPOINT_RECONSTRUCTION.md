# Diagnostic checkpoint working reconstruction

This working source replaces the lost local .106 implementation. It is based on
the restored .105 tree c5e2535598953b5eecb419a0e59764e4680e0d24, published as
040aa9381c370d51110d62e04b17e61daf231995. It is newly reconstructed source, not
an assertion of byte identity with the lost a034548d source or its test results.

The optional append-only app service copies a bounded 768-byte checkpoint into
ordinary RAM. Runtime derives normalized application identity and validates the
owner and current invocation token. Retained, inactive, provider lifecycle,
stream-busy, yielding, promotion and pending handoff calls are denied. The
backend performs no storage, allocation, provider calls, observer/drain calls,
or live output. The existing RTC journal layout is unchanged. A later `diag`
request after console restoration snapshots the record and replays through the
existing bounded transport. Reset/power loss discard this ordinary-RAM record.

Current fresh checks: original journal/native/HWCDC suite; reconstructed bounded
copy/truncation and native owner/no-I/O/replay tests normally and with ASan/UBSan.
Actual Runtime lifecycle fixture, old short-allocation table boundary and real
PHY-fenced capture/post-CDC replay also pass normally and with ASan/UBSan.
LeakSanitizer is disabled because the execution transport uses ptrace. Target
build and independent review remain pending. This is a
working checkpoint, not a qualified product image. USB provider/app consumers
are separate commits and are not included here.

Source-bound commands: `bash test/run_diagnostic_checkpoint_test.sh`,
`bash test/run_diagnostic_checkpoint_runtime_test.sh`,
`bash test/run_diagnostic_journal_test.sh`, `bash test/run_usb_phy_test.sh`.
Repeat with `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1`.
