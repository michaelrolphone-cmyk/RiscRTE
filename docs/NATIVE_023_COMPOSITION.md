# Runtime 0.2.3 composition

Extends the 0.2.2 native runtime with the explicitly scoped shared-data file
capability and timestamped RF worker from temporal integration a9587bec.
Keeps the newer asynchronous Wi-Fi, storage resource guards, TCP, diagnostic
checkpoint, app-data exports, and all retention barriers.

Shared grants bind one exact basename and namespace; ordinary exclusive
app-data ownership stays unchanged. Worker stop must join before release or
unmapping. A failed stop pins the provider and blocks storage, exit and sleep.
Healthy worker capture allows owner-side storage while preventing other RF
users and all exit/sleep transitions. RX-only I2S remains eligible for the
shared multi-source detector; X4 selects RF only.

The cohort profile has 28 providers and 44 graph grants, retaining 16 shared
non-boot grants. Opt-in 24-row immutable app policies and requirements support
shared files; invocation grants remain 16. Legacy static targets keep their
original provider limits. Host tests and native builds are not hardware tests.

The append-only Runtime API now offers `monotonic_ms`. Native owner calls read
`millis()` without heap enumeration, partition lookup or EFUSE identity reads.
Older ports use the existing health fallback. Current owner, active invocation,
null-output and terminal retention checks still apply. Renderer timing and
provider-poll timestamps use this path. No provider capability or raw import
is added. The shared-data ELF test checks 2,000 clock reads without any health
calls, wrong-owner/inactive rejection; retained-yield cases reject the suffix
after invocation retention (ASan/UBSan, leak scanning disabled in container).
