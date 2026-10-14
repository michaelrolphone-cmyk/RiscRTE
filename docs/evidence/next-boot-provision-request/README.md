# Next-boot request checkpoint verification

Date: 2026-10-14. Independent base:
`78a99f278015e35bc96a025cbb6672285b306197` (Runtime 0.2.23).
These are source-checkpoint checks, not a combined-product or hardware release.

Passed commands:

```sh
bash test/run_request_queue_test.sh
bash test/run_native_provision_request_test.sh
bash test/run_preserving_policy_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_request_queue_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_native_provision_request_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_preserving_policy_test.sh
BOOTLOADER_FILE="$PAIRED_BOOTLOADER" bash test/run_native_bank_test.sh
APP_DATA_TEST=1 SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 \
  BOOTLOADER_FILE="$PAIRED_BOOTLOADER" bash test/run_native_bank_test.sh
bash test/run_provisioning_test.sh
python3 scripts/check_versions.py
python3 test/compile_native_provision_request_target.py \
  --packages "$PLATFORMIO_PACKAGES" --output "$TARGET_OBJECTS"
```

The paired test bootloader is 15104 bytes, SHA256
`2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363`.
It is the existing Watch expanded paired build bootloader. Native tests model
flash/VFS boundaries and execute the actual transaction/admission code. The
normal full run covers ABI1; the app-data sanitizer run covers ABI2. Leak
checking is disabled because retained ownership is intentional in fault cases;
address and undefined-behavior checks are enabled. Each command completed with
exit status zero. Logs are adjacent to this file.

The target check compiles the queue, adapter, policy, installer, Runtime, native
bank/boot/package-volume bridges and main translation units with the installed
ESP32-S3 compiler/SDK, one subprocess at a time. It does not link a complete
firmware or measure a whole call-chain stack bound. No device was accessed.

The source version check without a comparison base passes. This branch keeps
Runtime 0.2.23 to compose independently with the other T023 checkpoints; the
combined integration must select its version. Comparing these changed firmware
inputs directly with the same-version base requires a version increase under
the repository's PR version gate.

See `docs/NEXT_BOOT_PROVISION_REQUEST.md` for the separate app-fallback and
compact-profile dependencies, the unverified SD-byte limitation, and remaining
product/hardware qualification. No partition, foreground reset, owner input,
raw NVS erase, or public catalog/bank-store prefix change is introduced.
