# ESP32-S3 Infrastructure Agent v1.4.2

Fixes a terminal Capabilities request rejection stopping relay enrollment and heartbeats. A saved INVALID_REQUEST is retired only when the authenticated response matches its operation ID and confirms not_applied. A redacted rejection record is committed atomically; unknown outcomes retain custody. relay.health reports participant_INVALID_REQUEST. Observation checkpoint resets now start at verified wall time; other participant failures retain execution custody in quarantine while relay and required maintenance continue.

## Existing v1.4.1 device: preserve the enrollment key

Download **riscrte-esp32s3-infrastructure-v1.4.2-repair.zip**. Do not use the initial image on a device with queued sealed enrollment. Extract the bundle and, with Python 3, esptool and a C compiler available, close the browser USB connection and run from the extracted directory:

```sh
python3 scripts/repair_infrastructure.py --bundle . --port YOUR_SERIAL_PORT --backup /PRIVATE/NEW_BACKUP_DIRECTORY
```

This reads a complete private backup, prints the saved pending action and operation ID without its payload, verifies the new pair, migrates only the exact v1.4.1 implementation pins while preserving all credentials/enrollment keys/requests/cursors, and activates the pair last. Reset after verified completion and keep the backup private. The firmware then reconciles the original saved request and can process the existing sealed enrollment. No bus enrollment was replaced or acknowledged during release preparation.

The current catalog remote-flash API only supports full-image replacement; it cannot perform this repair. The initial .bin is for a fresh installation and erases saved data.

## Validation

ASan/UBSan infrastructure suite passes (LeakSanitizer disabled because this executor cannot inspect /proc task state): 173 service calls, 28 maintenance wakes, four reboot scenarios. Tests cover terminal rejection custody, redaction, continued health, checkpoint loss against the exact deployed Capabilities source, portal, encryption/enrollment, and real filesystem journal recovery. The offline LittleFS repair preserves private identity and pending requests. Target build and package verification pass. Physical recovery/enrollment acceptance remain pending; this release does not claim a live hardware pass.
