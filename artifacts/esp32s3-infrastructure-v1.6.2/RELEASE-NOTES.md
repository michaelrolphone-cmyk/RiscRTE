# ESP32-S3 Infrastructure Agent v1.6.2

Fixes the observed v1.6.0 crash-loop path: a missing Capabilities provider caused NOT_FOUND, stopped the independent relay, and deprived a responsive blocked state of watchdog feeds. The 90-second native watchdog then reset it after 120 seconds in that state.

Missing inventory providers now retry at 60-second intervals while relay and required maintenance continue. New work admission and installation health remain blocked until inventory succeeds. Exact provider/package pins and execution custody remain intact. Responsive blocked states keep watchdog feeds; stalled asynchronous operations remain protected. The 30-second service/relay HTTP budgets from v1.6.1 are retained.

Validation: full infrastructure host suite; actual Capabilities/Scheduler integration with two missing-provider rejections, continued maintenance and subsequent recovery; address/undefined sanitizers on recovery (LeakSanitizer unavailable in this container); v1.6.0/v1.6.1 preserving LittleFS migrations; pinned ESP-IDF 6.1 target build and all three ELF import checks. Physical 1.6.2 flash, 60-minute single-boot soak, cursor catch-up and new-enrollment completion remain pending.

The missing v1.6.0/v1.6.1 provider definitions were repaired in the live catalog. Release publication also registers and reads back the exact built v1.6.2 definitions before catalog availability.

For an existing installation, use the preserving repair ZIP. It preserves Wi-Fi settings, credentials, enrollment keys, subscriptions, cursors and pending custody. The complete 16 MiB initial.bin is for first installation at offset 0x0; flashing it replaces saved device data. Publication does not flash hardware.

Runtime: f1b0c43eaa18ff099393a98d3a679e8319c512dd
System Apps: 13135a5a86b6b672ad47012dce62daf3d3b2b706
