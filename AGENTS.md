# RiscRTE migration boundaries

Keep this repository a minimal headless runtime. Read README.md and
`docs/HARDWARE_CONTRACT.md` before changing boot, binding or lifecycle behavior.
Preserve the shared hardware ABI exactly; add explicitly versioned materializers,
not incompatible layouts. Never load drivers before the full board/config graph
is validated. Missing/corrupt modules fail closed; never format storage on boot.

Retain mapped code, typed configuration and dependencies after failed quiescence.
App handoffs unload before launching and return to a fresh default invocation
when a child exits/fails. No firmware UI, product functionality, broad peripheral
migration or implicit package privilege. Preserve source licensing/provenance.

Use single-job target builds. Host tests and target builds do not qualify hardware.
No serial/device access, flash, merge or release without explicit owner direction.
