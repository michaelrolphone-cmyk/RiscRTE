# Optional native startup status (Runtime 0.1.57)

A separately composed native platform can provide the optional C symbol
`const char* risc_native_startup_error(void)`. Generic Runtime does not provide
pin assignments, call a product initializer, or depend on this symbol being
present. The default/Watch build retains its existing startup behavior.

After CPU owner setup and the normal nonwaiting diagnostic initialization,
`setup` checks the symbol once. A null return permits the existing boot sequence.
Any non-null return is the platform's startup failure: Runtime emits
`RTE_BOOT error=native-startup detail=<text>` and returns to the yielding idle
loop before bank preparation, storage mounting, provisioning, graph admission
or provider/application execution. It does not attempt recovery or roll back an
uncertain platform resource. The platform owns the static lifetime and content
of the returned string. It should report an unattempted initialization as failure.

This supports native board initialization through the framework's existing early
hook without putting product pin policy in Runtime. It neither expands provider
or app authority nor relaxes hardware/graph validation. Composed binaries need
both Runtime and platform source identities; a generic Runtime commit alone is
not complete provenance for such a binary.

`python3 test/test_native_startup.py` compiles the actual `src/main.cpp` with a
native framework/backend shim. It checks absent, successful and failed strong
status symbols, exact call ordering, diagnostic failure detail, and yielding
idle after rejection. Host tests and linked-target checks do not qualify reset
or power-latch timing on physical hardware.
