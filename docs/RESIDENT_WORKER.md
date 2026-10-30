# Resident worker implementation boundary

Task9838564c remains8points, selected after completed designead329bb and panelc69. Current Runtime authority is main13eca114. Storage-specific mailbox/admission and existing native ABI2 imports remain unchanged.

The implementation exposes a distinct `platform.resident-worker@1` dependency, explicitly selected per board-device provider. CpuPort registers no global alias and no raw app grant; existing complete-graph registration/admission applies. Native-only hardware supplies the shared resident authority; selected capability with absent backend refuses boot. No current board/profile silently selects it.

Four internal-DRAM descriptors carry device scope and monotonic generations. Owner-only start/cancel/join/release leave running or completed-but-unjoined descriptors live. Failed start guarantees no future callback; target SDK creation proof is pinned tofff9895 and must be checked in hosted target configuration. Successful join acquires completion published after callback returns; release requires that observed join. Cancellation is cooperative, never deletion of executing ELF. Caller must keep argument, code and dependencies stable until join, return normally, and install no deferred ELF TLS/deletion callbacks. Join does not establish DMA/IRQ idle or completed kernel reclamation.

Native envelope is internal allocated and freed before dispatch. Core's completion store is its last slot access; trampoline and task self-delete remain resident. ProviderStorageSafe, native discovery, app-exit/restart (via existing provider safety), quiescent, and actual native light/deep entry refuse live descriptors. Fenced ports refuse new starts but owner cancel/join/release can drain custody. The old storage worker is not generalized in place or granted to display.

Validation uses actual core with real std::thread concurrency, actual native trampoline with lowest FreeRTOS allocation/scheduling fault model, real Runtime/CpuPort selected-device binding, and actual dlopen provider/app plus Runtime/Graph proving unload refusal with live code. A host pass is not target execution or physical proof. Full native SDK target build and source-bound receipts remain required.

Current tests and core are implementation, not full EPD engine migration. Engine adaptation, PSRAM/cache, DMA allocator and product qualification stay their separately tracked tasks.
