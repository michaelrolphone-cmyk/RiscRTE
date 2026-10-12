# Atomic settings authority in staged cohort admission

The settings-selected Runtime copies its exact registered native endpoints into
a fresh staged Runtime through the existing registration functions. It does not
run native binding again, copy active grants or invocation state, or change an
ABI or object layout. Registration rejects queued work and retained custody.
Availability callbacks retain their original context and function pointers.

A candidate with any preseeded atomic binding is rejected before preparation.
This prevents staged admission from combining a caller-supplied authority table
with the trusted running Runtime's table. A failed partially registered candidate
is likewise unusable for a second admission; callers must use a fresh candidate.

The focused test covers all eight selected registrations: namespace 1 and 6
read-through entries with independent availability contexts, the existing
settings, paper_clock, battery, ble_scanner and contexts writers in namespace 1,
and wifi_settings in namespace 6. It asserts exact pointer/identity preservation,
no active contexts, no rebinding, no availability queries or backing I/O during
admission; rejection of preseeded, pending and retained states; and successful
fresh admission after clean collection. Its payload callback is intentionally
inert. Real target ELF and cumulative product cohort admission are separate
product qualification gates.

Run `bash test/run_atomic_key_value_cohort_test.sh` and
`ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_atomic_key_value_cohort_test.sh`.
The CI settings matrix runs both along with the existing Runtime atomic suite.
These are software checks, not physical-card or power-interruption evidence.
