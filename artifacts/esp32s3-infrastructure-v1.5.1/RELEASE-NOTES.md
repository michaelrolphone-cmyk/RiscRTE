# ESP32-S3 Infrastructure Agent v1.5.1

Activation delivery now takes assignment, deadline and effect identity from the matching authenticated Capabilities event. Mismatched subscription, routine version, generation and malformed activation requests are rejected. Retries retain the same invocation identity and shared relay custody.

The provider cohort is revision 9. The preserving repair supports published v1.4.1, v1.4.2 and v1.5.0 enrollments; Wi-Fi, device keys, credentials and pending requests remain intact. The native Runtime is unchanged.

Host sanitizer tests, independent signature checks, real Capabilities/Scheduler recovery integration, target ELF build, bundle integrity and full image verification pass. Leak detection is disabled because the execution environment does not support its process inspection. Physical USB, hardware cadence and actual native ChatGPT agent start/outcome timing remain unqualified; this is a prerelease.

Use the repair ZIP for existing enrolled devices. The initial binary is for first installation and replaces all 16 MiB. No device was flashed during this release.

Runtime: `e04c42a9b09c8e90444e20dc61006db4a83b01fc`

System Apps: `58af134db9a98a5c3b7c4b98b71bb015fc19abf4`
