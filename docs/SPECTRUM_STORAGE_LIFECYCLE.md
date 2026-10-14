# Spectrum storage lifecycle verification

Audio Spectrum repair is owned by RiscRTE-Utilities PR #68, commit
5d8b116c43887931d9c2d3db1eeb31f20ef806b5. Each complete-file operation borrows
and releases the invocation's single native app/shared-data slot. The stable
app-owned proxy does not preserve native contexts after release.

Runtime acquisition exclusivity, appDataContext revocation and terminal
retain_invocation remain unchanged. The native custody adapter intentionally
retains on uncertain acquisition; allowing concurrent native storage leases or
restoring storage authority after retention would weaken that contract.

The real shared-library regression additionally exercises repeated private/shared
interleaving, empty refusal output, exact shared-file authority and stale-context
rejection while a later grant is live. Run `bash test/run_shared_data_test.sh`.
Host tests do not establish physical-device execution.
