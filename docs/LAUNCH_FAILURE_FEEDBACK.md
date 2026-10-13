# Recoverable launch feedback

The append-only `take_launch_failure` Runtime API copies and consumes one recoverable child failure for the configured default during app_main. It performs no media or provider I/O and grants no authority. It is unavailable during init/fini, retained custody, foreign ownership or a queued handoff. Older tables remain compatible by suffix-size checks.

The normal default reload path captures the bounded relative application name and diagnostic after completed cleanup. External-image load failures carry `RISC_LAUNCH_FAILURE_EXTERNAL_IMAGE`; shared Home chooses the user-facing text. Retained cleanup never launches Home merely to show an error. Resident lifecycle and sleep behavior remain unchanged.

Actual removable Runtime/ELF tests cover absent, missing, busy, removed and malformed media, normal/replacement launches, consume-once and retained-close behavior. Host tests do not establish hardware qualification.
