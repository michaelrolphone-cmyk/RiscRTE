# Explicit shared SD settings candidate (Runtime 0.2.20)

This candidate extends format-2 settings to a boot-authorized list:

```json
"installation": {
  "volume_instance": 3,
  "namespace_range": {"first": 3000, "count": 128},
  "settings_sd": true,
  "shared_settings": [1, 6],
  "grants": []
}
```

The example volume is illustrative; products must keep their existing admitted
volume and grant envelope. No existing product is opted in by this Runtime
change. The matching installed-apps service and Files/WebDAV protections must be
built and qualified before enabling shared_settings in a product image.

## Storage and authority

Each selected namespace uses the existing scalable map and durable native
per-key recovery journal. Both app KV and explicitly bound provider KV route
through the same state. Keys remain individually bounded by the established
ABI, but no sixteen-key namespace ceiling is introduced. Existing native values
are enumerated without erasure. Unselected namespaces retain native behavior.

Shared checkpoint slots follow the private slots. The appended installation
suffix maps these slots to namespaces without granting new app/provider access.
The checkpoint owner is exactly shared-%08x. Paged checkpoints live under
/RTECFG/%08X; private installed-app paths stay unchanged. RTECFG is an 8.3-safe
name, with case and trailing-dot/space aliases rejected by the shared path guard.

Migration first validates or recovers the SD index, then reconciles the native
journal. Immutable pages are verified before atomic index replacement; native
acknowledgement follows verified readback and checked worker close. A newer
concurrent native write remains dirty. A full native erase can restore a valid
SD snapshot without a device-local encryption key. Missing or corrupt anchored
checkpoints fail closed rather than reviving stale native defaults.

Before cold restoration completes, selected namespace reads and writes report
BUSY. After restoration, writes can be accepted into the durable native journal
while the card is absent or exported. Those writes are not yet SD-durable and
will not survive a subsequent full flash until checkpoint completion. UI must
distinguish native save from confirmed SD checkpoint. Export/removal must use
the existing copied-worker cancellation and checked-close custody protocol.

## Confidentiality limits

These are plaintext recovery records. The matching Files and WebDAV builds
must exclude RTECFG from listing, direct paths, copy, recursive operations and
mutation. This protects normal app sharing, not physical access to the removable
card or raw block-device USB export. No passphrase workflow or erased-device
key is required. CRC detects accidental corruption, not malicious replacement.
Do not put actual network names, passwords or record bytes in logs or evidence.

## Qualification

The source adds real Runtime app/provider routing and cold-restoration
regressions using synthetic opaque records: namespaces 1 and 6, preserved legacy
values, more than 1024 keys, unselected native namespace isolation, wrong-owner
rejection, and native erase/restart with unchanged SD snapshot. Existing scalable
journal, uncertain-write, concurrent-checkpoint and CRC tests remain applicable.
Run test/run_removable_runtime_test.sh and test/run_sd_first_key_value_test.sh.
Host tests do not qualify target builds, actual card power loss or device behavior.
