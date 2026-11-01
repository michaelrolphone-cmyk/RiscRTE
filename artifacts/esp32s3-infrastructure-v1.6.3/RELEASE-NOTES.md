# ESP32-S3 Infrastructure Agent v1.6.3

Repairs two reproduced reliability defects: transient participant HTTP/body errors could permanently silence relay health, and the journal writer could commit nested state that its reboot loader rejected. Participant retries now have a bounded 60-second maximum delay while independently authenticated relay reporting continues. Pending operation identities remain unchanged. Journal writes and reads use the same bounded envelope; network validation remains separate. Missing/read/checksum/JSON/schema failures now have distinct redacted diagnostics.

The owner's November 1 USB trace confirms a mounted filesystem followed by StorageBlocked during initial journal load. The actual private journal has not been examined, so neither reproduced defect is asserted as that device's proven physical trigger. Preserve its flash before repair. The bundle's existing installer preflight mode makes a private read-only backup; inspect_infrastructure_state.py --diagnose produces a redacted checksum/shape report.

Validation: complete infrastructure suite with AddressSanitizer and UndefinedBehaviorSanitizer; old-source failure/new-source pass for journal restart; three preserving migration cases from 1.6.0/1.6.1/1.6.2; 13 installer recovery tests; two redacted diagnostic tests; exact ESP32-S3 three-ELF target build and import checks. LeakSanitizer is unavailable in this container. All 43 hosted Apps checks passed at cfde380. Exact revision 14 provider definitions were published and read back before this release.

For an enrolled board use the preserving repair ZIP. Full initial.bin at 0x0 replaces saved data and is for initial installation. The repair refuses unreadable/corrupt input; it does not silently create a new journal. Native runtime binaries are reused byte-for-byte from the verified 1.6.2 release. No device was flashed by this publication. Physical recovery, 24-hour soak and seven-day uptime qualification remain pending.

Runtime: f1b0c43eaa18ff099393a98d3a679e8319c512dd
System Apps: cfde380bc3d202a176e381251917d6d3abbd6fe0
