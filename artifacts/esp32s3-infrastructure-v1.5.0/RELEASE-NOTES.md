# ESP32-S3 infrastructure v1.5.0

Shared relay-delivery/1 delivery supports webhook and ChatGPT MCP Events destinations. Sealed bus enrollment, callback verification and one owner decision gate new destinations. The MCP front door uses the same enrollment records; the ESP32 is the only HTTP delivery executor.

Eight subscriptions/sites, independent persisted retry custody, global ordering across each subscription's channels, redacted relay.delivery v1 receipts, per-subscription health, and terminal MCP 410/413 handling are implemented. The runtime adds checked public-DNS POST without changing HTTP ABI layout. Captive portal and saved INVALID_REQUEST recovery remain covered.

Use the **repair ZIP** on installed devices. It preserves Wi-Fi, participant credentials, the X25519 identity, installed Grok enrollment and pending sealed bus requests. It writes the inactive firmware/application pair, preserves private journal state and activates last. Never use the full first-install image on an enrolled device.

The exact published v1.4.2 bootloader is accepted by hash and left untouched. Its executable bytes are identical to the new build; only the build timestamp and checksum/digest differ.

Software validation: sanitizer firmware suite with real Capabilities/Scheduler service harness; 215 native HTTP sanitizer cases and 206 legacy HTTP cases; 46 Bus tests including 301-event independent catch-up and multi-channel ordering; independent signature checks; preserving phone filesystem and transport tests. Hardware boot, live backlog timing and native ChatGPT activation remain unverified. Callback HTTP acceptance never claims agent start or completion. Full unified agent-outcome/fence/failover and remaining producer callback requirements are not claimed complete by this firmware release.

Source versions are recorded in flash-manifest.json and published on the linked source branches. The release workflow verifies the complete image and downloads every published asset to recheck its checksum.
