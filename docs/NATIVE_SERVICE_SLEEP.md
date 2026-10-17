# Invocation-owned native service sleep transaction

The append-only Runtime v1 suffix exposes sleep_prepare(token) and sleep_resume(token). Shared Home calls prepare after completing its blank display frame, before power/wake/pad preparations. Every ordinary sleep refusal or Light wake restores physical wake/pad holds before resuming the Runtime transaction. Successful Deep is terminal. A retained result stops further app/provider cleanup and pins custody.

The owner-only app_main operation checks actual stream, file, preference, lifecycle and native service idleness. It settles serial work at the existing explicit checkpoint, validates the complete set of mapped native volume sleep interfaces, freezes their internal file clients, then prepares and commits each through its owning provider. A failed operation rolls back in reverse order; uncertain rollback retains the invocation and graph. Missing sleep interfaces refuse the transition rather than bypassing a live controller. Runtime neither infers a backend nor performs SD I/O in CPU readiness.

The token is invocation-bound, monotonically generated and single-use. Wrong owner/token/context, queued launches and reentrant callbacks cannot use it. An abandoned token retains the invocation before fini. Runtime service dispatch and graph polling stay suspended until ordinary resume. An unavailable removed card is a clean resume result; no alternate store or formatting is attempted.

Reversible Light preparation permits a resident foreground stack. Actual CpuPort Deep/reset admission retains residentResetSafe and refuses losing that live stack. A mapped SD card must still be closed and every native readiness guard must pass at final Deep entry.

Historical Runtime prefixes have no suffix: the SDK helper does not read beyond struct_size and preserves their original path with token zero. This compatibility does not retrofit the SD fix into old firmware.

Host tests: test/native_sleep_transaction.py composes production Runtime, serial service, actual Wave provider/FatFs, CpuPort and NativeSdmmc against RAM media and mocked hardware. Cold, absent and mounted media cover repeat transactions, context/handle refusals, nested callback fencing, resident Light versus Deep, and modeled terminal Deep. An abandoned-token control confirms retained graph and frozen dispatch. Shared Home caller tests separately cover old-prefix bounds, screen restoration, provider cleanup ordering and retained results. These tests do not establish linked-memory fit, native product acceptance or physical sleep/wake.
