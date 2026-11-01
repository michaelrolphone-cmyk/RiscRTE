# ESP32-S3 Infrastructure Agent v1.6.1

Corrects v1.6.0 failures on slow Internet: control and relay HTTP requests now have a 30-second total deadline, replacing five seconds. Native HTTP V2 honors that full deadline without a hidden 15-second idle cutoff. Error logs distinguish timeout from other transport errors and include phase, elapsed time and sent/received byte counts without credentials or request bodies.

Software regressions passed with native responses delayed by 10, 20 and 29 seconds, a service/relay response delayed by 12 seconds, and expiration at 30 seconds. The native HTTP, infrastructure and preserving migration suites passed. Pinned ESP-IDF 6.1 compilation and three ELF import validations passed. Actual slow-network, power/AP and elapsed outage qualification remain pending.

For an existing installation, the repair ZIP contains the preserving workstation update. It recognizes the exact published 1.6.0 provider set and preserves Wi-Fi settings, credentials, enrollment keys and pending custody. Existing 1.6.0 pairing files are accepted by first setup.

The initial.bin is the complete 16 MiB flash-ready image for the ESP32-S3 Infrastructure Agent profile at offset 0x0. Full-image flashing replaces saved device data. Publication does not flash hardware.

Runtime: f1b0c43eaa18ff099393a98d3a679e8319c512dd
System Apps: 36117ce59c83ff4f55180a06b9dcc0a66a9340d3
