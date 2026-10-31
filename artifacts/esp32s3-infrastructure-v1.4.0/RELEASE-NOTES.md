ESP32-S3 infrastructure agent v1.4.0 replaces the portal-dependent webhook setup with encrypted message-bus enrollment.

A subscriber reads the device public identity, verifies its fingerprint against the Capabilities participant, seals its private URL path and credentials locally, and publishes relay.enroll. One authenticated Decisions approval installs the subscription. New origins and secret references are created in the same durable journal transaction. Rotate and revoke use new approvals. The old unapproved relay.subscriptions replacement message is ignored.

The release includes RELAY-CONTRACT.md and relay-enroll.mjs. Grok Worker 1 should use its private stored URL, Authorization header and signing key with that helper; no secret belongs in a public message or task. After the firmware is installed, each new webhook needs no captive portal, reflash or pairing-file edit. The existing v1.3.0 pairing file remains accepted for initial setup.

The Channel Bus service has been deployed with the scoped enrollment route and participant binding. Matching-event catch-up now examines up to 25 events at a time without skipping the next match. The v1.3 Scheduler timeout correction remains included.

Verification: 42 Channel Bus tests, actual Capabilities decision-engine integration with signed phone responses, compiled Worker/D1 scope checks, independent Node/OpenSSL-to-firmware encryption vectors, ASan/UBSan node tests, and 173-call maintenance regression with 28 wakes and four reboots pass. ESP32 target ELFs and native runtime compile; full-image component, source, paired-bank and embedded ELF hashes verify. Source commits are pinned in the manifests.

Hardware qualification remains open: initial installation, live relay.identity, actual Grok enrollment and callback, approximately 10-second delivery, outage/reboot behavior and physical memory/stack headroom. Software validation does not claim those results.

Download riscrte-esp32s3-infrastructure-v1.4.0-initial.bin directly. Target ESP32-S3 with 16 MiB flash and octal PSRAM, offset 0, paired-appdata-v2 ABI 2. FIRST-INSTALL IMAGE: writing all 16 MiB replaces saved Wi-Fi, pairing and device data. It is not a data-preserving update. Provision the existing private pairing once after initial installation; webhook registrations thereafter use only the bus and Decisions. No credentials are embedded in this public image.
