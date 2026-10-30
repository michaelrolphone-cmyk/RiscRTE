# Native worker execution fixture (partial criterion coverage)

This test-owned `app_main` wrapper obtains the actual `nativeHardware(owner)` resident authority and exercises real FreeRTOS scheduling, early completion, join timeout, cancellation, stale/scope/owner refusal and retained descriptors. It neither creates a duplicate header-local authority nor substitutes SDK functions. It uses existing `RISC_NATIVE_EXTRA_SOURCES` and `RISC_NATIVE_LINK_OPTIONS` hooks; its output requires an exact source SHA.

The fixture intentionally does not call ordinary startup or touch a physical peripheral. It parks after success/failure with stable static callback storage. A callback completion store does not imply OS task/TCB reclamation. Failure preserves resources; it does not reset or free uncertain state.

It tests invalid-start/no-future-entry, not injected heap/RTOS-create exhaustion. It does not establish selected-provider admission, actual ELF unload/dependency custody, physical device compatibility or a flash authorization. Those remain separate from these seven direct-authority cases. Compilation is not execution: require actual serial case records from an authorized supported target before claiming any target case passed.

The prior host tests retain allocation/create failure and actual Runtime/Graph/dlopen custody coverage. A native selected-provider fixture must replace host-private trace/mode imports with typed test configuration/results and use the existing embedded provider-loading route; this wrapper alone cannot close that remaining criterion.
