# Shared AppData capacity qualification

This independent campaign uses the unchanged native Settings backend, checked immutable-bank store, captured all-writer AppData mount and pinned real LittleFS on the existing 0x80000-byte partition at 0x270000. The existing resource-refusal fixture and runner are unchanged.

The campaign covers ExternalVolume namespace 1/6 and LocalOnly namespace 6 with ordinary namespace1 legacy data. These are persistence-policy cases, not a claim that the host fixture reproduces a complete product, all installed writers, physical media or live heap. The existing host Assembly/worker doubles remain explicitly visible.

## Evidence produced

- Seed opaque legacy data plus peers in four ordinary AppData namespaces; preserve every peer file.
- Initialize using the existing checked legacy import path. Keep its source/archive obligations.
- Observe each real reserve call's exact image bytes, physical free bytes, rounded replacement requirement and unchanged 32 KiB recovery allowance. These numbers describe the implementation's admission check, not a new or persistent reservation.
- While the real Settings mount custody is held, attempt AppData custody from another thread before and after reserve. Both attempts must fail without taking or releasing the owner's lease.
- Refuse the store's staging allocation only after Core's image/scratch allocations and the real free-space check. Require NoMemory, released temporary allocations and unchanged selected data.
- Let ordinary peer files consume capacity between successful Wi-Fi mutations. Recheck admission on the next operation; once free space is below the existing reserve requirement, require NoSpace without root, legacy, peer or flash mutation.
- Never remove a peer/filler file to retry. No delete-to-fit path is introduced.
- Save full flash/NVS and reload in a separate process. Verify selected revision/sequence, opaque values, retained import provenance and peer bytes.
- In separate processes, cut immediately after the real immutable bank seal and before selected-root publication. Reload must retain the prior selected root and payload. The candidate bank and monotonic next-object metadata may differ; whole-flash equality is deliberately not required after this permitted physical work.

Normal and ASan/UBSan matrix jobs keep LeakSanitizer enabled on ordinary paths. The explicit process-cut case exits 77 without pretending to destruct retained process state. Each consumer depends on the matching producer snapshot. Timeouts, unexpected exit codes and sanitizer failures remain failures.

## Product boundary

Runtime source: 532c502fb2aad358d46166d5fd1df8358148f20e.

- X4 successor cf7583ca682649819c534d749ecc7d38ec72f63a selects ExternalVolume 9, namespace mask 3, seven exact bindings for six identities: settings, paper_clock, battery, ble_scanner, contexts and wifi_settings in namespace1, plus wifi_settings in namespace6. Its actual native link restored the prior 16-byte IQ margin without changed banks or limits. This does not measure live heap or filesystem occupancy.
- Watch qualification 9722ccf1c38962dec121c869c1928a9fa4ef3880 selects LocalOnly, volume 0, mask 2 and only wifi_settings 6; LocalFresh remains 0 and namespace1 preferences remain ordinary. Actual AppData offset 0x270000/length 0x80000 is verified from native partition evidence. No user-populated AppData/NVS snapshot is available.
- Wave native qualification 85072dad selects ExternalVolume 12, mask 3 and 23 bindings: 22 namespace1 writers plus wifi_settings6. Its larger writer population does not increase the shared capacity. No user-populated AppData/NVS snapshot is available. Its actual partition artifact confirms AppData offset 0x270000/length 0x80000, SHA256 d27edcdba637142baa7d91e29b70df47554c22a4f14de303dc6a0cdcc1e25de8 (run42033641272/artifact13417387630).

The synthetic peers are adversarial fixtures, not guessed product occupancy. This candidate is intended to test host fail-closed admission, shared-custody and selected-root recovery boundaries; those conclusions require passing results on the exact candidate. It does not establish an always-successful save guarantee, a durable cross-client reservation, all-writer route coverage outside the captured mount, hardware reboot behavior, live INTERNAL/PSRAM/stack headroom or usable free capacity on a particular device. Those require the exact product image, real current occupancy and all peer allocation/admission contracts. No product profile, partition, file-size limit, recovery allowance or release selection changes.
