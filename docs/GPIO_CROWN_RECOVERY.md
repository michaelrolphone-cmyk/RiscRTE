# Explicit GPIO recovery input

`RISC_NATIVE_RECOVERY_GPIO=1` selects a sampled switch input for the existing
native crown supervisor. Trusted provisioning must also select
`RISC_RECOVERY_GPIO_PIN` and `RISC_RECOVERY_GPIO_ACTIVE_HIGH`. There are no board
names or default pin assignments in the implementation. This selection is
mutually exclusive with the existing AXP2101 interrupt-history input.

The supervisor starts only after the ordinary validated provider graph claims
and configures that input through native GPIO. It does not claim or configure a
second owner, change PMIC registers, or require a device-specific application.
Closing the GPIO drains the sampler before the ordinary pad cleanup.

Debounced short release requests the existing child-invocation exit. The
existing three-second failed-cleanup fallback remains active. A continuously
observed five-second hold requests the same native emergency restart, independent
of foreground app cooperation. Initial held release is not a click; sampling
gaps and sleep break hold continuity. A raw release immediately cancels hold
evidence, before debounce can accidentally extend it past five seconds.

This adds no display suspend/resume or wake source support. Those remain provider
and validated sleep-configuration responsibilities. Host tests and qualification
target builds do not establish physical button timing or hardware sleep/wake.

Validation: `test/run_crown_recovery_test.sh` exercises both input selections;
`crown-build.ini` contains separate explicitly configured qualification targets.
